#include "idebus.h"
#include "../ioport.h"
#include "../pic.h"
#include <assert.h>
#include <errno.h>
#include <kernel/arch/iodelay.h>
#include <kernel/arch/mmu.h>
#include <kernel/dev/atadisk.h>
#include <kernel/dev/pci.h>
#include <kernel/io/co.h>
#include <kernel/lib/diagnostics.h>
#include <kernel/lib/miscmath.h>
#include <kernel/lib/pstring.h>
#include <kernel/mem/heap.h>
#include <kernel/mem/pmm.h>
#include <kernel/mem/vmm.h>
#include <kernel/types.h>
#include <stdalign.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define PRD_FLAG_LAST_ENTRY_IN_PRDT (1U << 15)

/******************************** Configuration *******************************/

/*
 * This is will reprogram Prog-IF if the card is in legacy mode and can be
 * switched to native mode. Note that I personally had mixed results with this:
 * - VirtualBox claims that it can be switched to native mode, but writing
 *   modified Prog-IF back didn't actually update it.
 * - Under HP Elitebook 2570p, it can be modified, and reports native I/O
 *   address through BAR, but the machine itself uses SATA. Older SATA drives
 *   may work, but mine is SSD and it didn't. The OS can't see the drive at all.
 *
 * And honestly, if legacy ports are there - Just use it. There's no real
 * advantage I can see with PCI native mode ports.
 *
 * Set to true to enable reprogramming.
 */
static const bool CONFIG_REPROGRAM_PROGIF = false;

/******************************************************************************/

/* Physical Region Descriptor */
struct prd {
    uint32_t buffer_physaddr;
    uint16_t len;   /* 0 = 64K */
    uint16_t flags; /* All bits are reserved(should be 0) except for top bit (PRD_FLAG_LAST_ENTRY_IN_PRDT) */
};

/* Shared data between two IDE buses in a controller */
struct shared {
    struct bus *buses[2];
    _Atomic bool dma_lock_flag;
    bool dma_lock_needed : 1;
};

struct bus {
    struct ArchI586_Pic_IrqHandler irq_handler;
    PHYSPTR prdt_physbase;
    struct prd *prdt;
    struct shared *shared;
    size_t prd_count;
    void *dma_buffer;
    uint16_t io_iobase;
    uint16_t ctrl_iobase;
    uint16_t busmastrer_iobase;
    PCIPATH pcipath;
    int8_t last_selected_drive; /* -1: No device was selected before */
    _Atomic bool bus_lock_flag;
    _Atomic bool got_irq;
    bool is_dma_read : 1;
    bool busmaster_enabled : 1;
};

struct disk {
    struct Ata_Disk atadisk;
    int8_t driveid;
    struct bus *bus;
};

typedef enum {
    IOREG_DATA,
    IOREG_ERROR = 1,    /* read */
    IOREG_FEATURES = 1, /* write */
    IOREG_SECTORCOUNT = 2,
    IOREG_LBA_LO = 3,
    IOREG_LBA_MID = 4,
    IOREG_LBA_HI = 5,
    IOREG_DRIVE_AND_HEAD = 6,
    IOREG_STATUS = 7,  /* read */
    IOREG_COMMAND = 7, /* write */
} IOREG;

#define DRIVE_AND_HEAD_FLAG_DRV (1U << 4)
#define DRIVE_AND_HEAD_FLAG_LBA (1U << 6)

static void io_out8(struct bus *self, IOREG reg, uint8_t data) {
    ArchI586_Out8(self->io_iobase + reg, data);
}
static void io_out16(struct bus *self, IOREG reg, uint16_t data) {
    ArchI586_Out16(self->io_iobase + reg, data);
}
static uint8_t io_in8(struct bus *self, IOREG reg) {
    return ArchI586_In8(self->io_iobase + reg);
}
static uint16_t io_in16(struct bus *self, IOREG reg) {
    return ArchI586_In16(self->io_iobase + reg);
}
static void io_in16_rep(struct bus *self, IOREG reg, void *buf, size_t len) {
    ArchI586_In16Rep(self->io_iobase + reg, buf, len);
}

typedef enum {
    CTRLREG_ALTERNATE_STATUS = 0, /* read */
    CTRLREG_DEVICE_CONTROL = 0,   /* write */
} CTRLREG;

#define DRVICE_CONTROL_FLAG_NIEN (1U << 1)
#define DRVICE_CONTROL_FLAG_SRST (1U << 2)
#define DRVICE_CONTROL_FLAG_HOB (1U << 7)

static void ctrl_out8(struct bus *self, CTRLREG reg, uint8_t data) {
    ArchI586_Out8(self->ctrl_iobase + reg, data);
}

static uint8_t ctrl_in8(struct bus *self, CTRLREG reg) {
    return ArchI586_In8(self->ctrl_iobase + reg);
}

static uint8_t read_status(struct bus *self) {
    return ctrl_in8(self, CTRLREG_ALTERNATE_STATUS);
}

static void bus_printf(struct bus *self, char const *fmt, ...) {
    va_list ap;

    Co_Printf("idebus(%x): ", self->io_iobase);
    va_start(ap, fmt);
    Co_VPrintf(fmt, ap);
    va_end(ap);
}

static void drive_printf(struct bus *self, int drive, char const *fmt, ...) {
    va_list ap;

    Co_Printf("idebus(%x)drive(%d): ", self->io_iobase, drive);
    va_start(ap, fmt);
    Co_VPrintf(fmt, ap);
    va_end(ap);
}

static void reset_bus(struct bus *self) {
    uint8_t regval = ctrl_in8(self, CTRLREG_DEVICE_CONTROL);
    regval &= ~(DRVICE_CONTROL_FLAG_NIEN | DRVICE_CONTROL_FLAG_HOB);
    regval |= DRVICE_CONTROL_FLAG_SRST;
    ctrl_out8(self, CTRLREG_DEVICE_CONTROL, regval);
    Arch_IoDelay();
    regval &= ~DRVICE_CONTROL_FLAG_SRST;
    ctrl_out8(self, CTRLREG_DEVICE_CONTROL, regval);
}

static void select_drive(struct bus *self, int8_t drive) {
    assert((0 == drive) || (1 == drive));
    uint8_t regval = io_in8(self, IOREG_DRIVE_AND_HEAD);
    if (drive == 0) {
        regval &= ~DRIVE_AND_HEAD_FLAG_DRV;
    } else {
        regval |= DRIVE_AND_HEAD_FLAG_DRV;
    }
    regval |= DRIVE_AND_HEAD_FLAG_LBA;
    io_out8(self, IOREG_DRIVE_AND_HEAD, regval);
    if (self->last_selected_drive != drive) {
        for (size_t i = 0; i < 14; i++) {
            read_status(self);
        }
        self->last_selected_drive = drive;
    }
}

static void atadisk_op_soft_reset(struct Ata_Disk *self) {
    struct disk *disk = self->data;
    reset_bus(disk->bus);
}

static void atadisk_op_select_disk(struct Ata_Disk *self) {
    struct disk *disk = self->data;
    select_drive(disk->bus, disk->driveid);
}
static uint8_t atadisk_op_read_status(struct Ata_Disk *self) {
    struct disk *disk = self->data;
    return read_status(disk->bus);
}
static void atadisk_op_set_features_param(struct Ata_Disk *self, uint16_t data) {
    struct disk *disk = self->data;
    io_out8(disk->bus, IOREG_FEATURES, data);
}
static void atadisk_op_set_count_param(struct Ata_Disk *self, uint16_t data) {
    struct disk *disk = self->data;
    io_out8(disk->bus, IOREG_SECTORCOUNT, data);
}
static void atadisk_op_set_lba_param(struct Ata_Disk *self, uint32_t data) {
    struct disk *disk = self->data;
    io_out8(disk->bus, IOREG_LBA_LO, data);
    io_out8(disk->bus, IOREG_LBA_MID, data >> 8);
    io_out8(disk->bus, IOREG_LBA_HI, data >> 16);
    uint8_t regvalue = io_in8(disk->bus, IOREG_DRIVE_AND_HEAD);
    regvalue = (regvalue & ~0x0fU) | ((data >> 24) & 0x0fU);
    io_out8(disk->bus, IOREG_DRIVE_AND_HEAD, regvalue);
}
static void atadisk_op_set_device_param(struct Ata_Disk *self, uint8_t data) {
    struct disk *disk = self->data;
    uint8_t regvalue = io_in8(disk->bus, IOREG_DRIVE_AND_HEAD);
    /*
     * Note that we preserve lower 3-bit, which contains upper 4-bit of LBA.
     * (ACS-3 calls these bits as "reserved", and maybe this is the reason?)
     */
    regvalue = (data & ~0x0fU) | (regvalue & 0x0fU);
    io_out8(disk->bus, IOREG_DRIVE_AND_HEAD, regvalue);
}
static uint32_t atadisk_op_get_lba_output(struct Ata_Disk *self) {
    struct disk *disk = self->data;
    uint32_t lba_lo = io_in8(disk->bus, IOREG_LBA_LO);
    uint32_t lba_mid = io_in8(disk->bus, IOREG_LBA_MID);
    uint32_t lba_hi = io_in8(disk->bus, IOREG_LBA_HI);
    uint32_t lba_top4bit = io_in8(disk->bus, IOREG_DRIVE_AND_HEAD) & 0x0F;
    return (lba_top4bit << 24) | (lba_hi << 16) | (lba_mid << 8) | lba_lo;
}

static void atadisk_op_issue_cmd(struct Ata_Disk *self, ATA_CMD cmd) {
    struct disk *disk = self->data;
    io_out8(disk->bus, IOREG_COMMAND, cmd);
}
static bool atadisk_op_get_irq_flag(struct Ata_Disk *self) {
    struct disk *disk = self->data;
    return disk->bus->got_irq;
}
static void atadisk_op_clear_irq_flag(struct Ata_Disk *self) {
    struct disk *disk = self->data;
    disk->bus->got_irq = false;
}
static void atadisk_op_read_data(struct Ata_DataBuf *out, struct Ata_Disk *self) {
    struct disk *disk = self->data;
    io_in16_rep(disk->bus, IOREG_DATA, out->data, sizeof(out->data) / sizeof(*out->data));
}
static void atadisk_op_write_data(struct Ata_Disk *self, struct Ata_DataBuf *buffer) {
    struct disk *disk = self->data;
    for (size_t i = 0; i < 256; i++) {
        io_out16(disk->bus, IOREG_DATA, buffer->data[i]);
        Arch_IoDelay();
    }
}

typedef enum {
    BUSMASTER_REG_CMD = 0,
    BUSMASTER_REG_STATUS = 2,
    BUSMASTER_REG_PRDTADDR = 4,
} BUSMASTER_REG;

static void busmaster_out8(struct bus *self, BUSMASTER_REG reg, uint8_t data) {
    assert(self->busmaster_enabled);
    ArchI586_Out8(self->busmastrer_iobase + reg, data);
}
static void busmaster_out32(struct bus *self, BUSMASTER_REG reg, uint32_t data) {
    assert(self->busmaster_enabled);
    ArchI586_Out32(self->busmastrer_iobase + reg, data);
}
static uint8_t busmaster_in8(struct bus *self, BUSMASTER_REG reg) {
    assert(self->busmaster_enabled);
    return ArchI586_In8(self->busmastrer_iobase + reg);
}
static uint32_t busmaster_in32(struct bus *self, BUSMASTER_REG reg) {
    assert(self->busmaster_enabled);
    return ArchI586_In32(self->busmastrer_iobase + reg);
}

#define BUSMASTER_CMDFLAG_START (1U << 0)
#define BUSMASTER_CMDFLAG_READ (1U << 3)

#define MAX_TRANSFTER_SIZE_PER_PRD 65536
#define MAX_DMA_TRANSFER_SIZE_NEEDED (ATA_MAX_SECTORS_PER_TRANSFER * ATA_SECTOR_SIZE)

static bool atadisk_op_dma_begin_session(struct Ata_Disk *self) {
    struct disk *disk = self->data;
    struct bus *bus = disk->bus;
    if (!bus->busmaster_enabled) {
        /* No DMA support */
        return false;
    }
    if (!bus->shared->dma_lock_needed) {
        /* No DMA lock is used - We are good to go. */
        return true;
    }
    /*
     * If DMA lock is present, that means both IDE channels can't use DMA at the same time.
     * So we must lock the DMA, and if we can't, we have to fall back to PIO.
     */
    bool expected = false;
    bool desired = true;
    if (!atomic_compare_exchange_strong_explicit(&bus->shared->dma_lock_flag, &expected, desired, memory_order_acquire, memory_order_relaxed)) {
        /* Someone else is using DMA */
        return false;
    }
    return true;
}

static void atadisk_op_dma_end_session(struct Ata_Disk *self) {
    struct disk *disk = self->data;
    struct bus *bus = disk->bus;
    assert(bus->busmaster_enabled);
    if (!bus->shared->dma_lock_needed) {
        return;
    }
    bool desired = false;
    atomic_store_explicit(&bus->shared->dma_lock_flag, desired, memory_order_release);
}

static void atadisk_op_lock(struct Ata_Disk *self) {
    struct disk *disk = self->data;
    struct bus *bus = disk->bus;
    bool expected = false;
    bool desired = true;
    while (!atomic_compare_exchange_strong_explicit(&bus->bus_lock_flag, &expected, desired, memory_order_acquire, memory_order_relaxed)) {
    }
}

static void atadisk_op_unlock(struct Ata_Disk *self) {
    struct disk *disk = self->data;
    struct bus *bus = disk->bus;
    bool desired = false;
    atomic_store_explicit(&bus->bus_lock_flag, desired, memory_order_release);
}

[[nodiscard]] static int atadisk_op_dma_init_transfter(struct Ata_Disk *self, void *buffer, size_t len, bool is_read) {
    struct disk *disk = self->data;
    struct bus *bus = disk->bus;
    assert(bus->busmaster_enabled);
    assert(len <= MAX_DMA_TRANSFER_SIZE_NEEDED);
    bus->dma_buffer = buffer;
    bus->is_dma_read = is_read;
    size_t remaining_size = len;
    for (size_t i = 0; remaining_size != 0; i++) {
        assert(i < bus->prd_count);
        size_t currentsize = remaining_size;
        if (MAX_TRANSFTER_SIZE_PER_PRD < currentsize) {
            currentsize = MAX_TRANSFTER_SIZE_PER_PRD;
        }
        if (currentsize == MAX_TRANSFTER_SIZE_PER_PRD) {
            bus->prdt[i].len = 0;
        } else {
            bus->prdt[i].len = currentsize;
        }
        if (remaining_size <= MAX_TRANSFTER_SIZE_PER_PRD) {
            /* This should be the last PRD */
            bus->prdt[i].flags |= PRD_FLAG_LAST_ENTRY_IN_PRDT;
        } else {
            bus->prdt[i].flags = 0;
        }
        if (!is_read) {
            PMemCopyOut(bus->prdt[i].buffer_physaddr, &((uint8_t *)buffer)[i * MAX_TRANSFTER_SIZE_PER_PRD], currentsize, true);
        }
        remaining_size -= currentsize;
    }
    /* Setup busmaster registers */
    busmaster_out32(bus, BUSMASTER_REG_PRDTADDR, bus->prdt_physbase);
    uint8_t cmdvalue = 0;
    if (is_read) {
        cmdvalue |= BUSMASTER_CMDFLAG_READ;
    } else {
        cmdvalue &= ~BUSMASTER_CMDFLAG_READ;
    }
    busmaster_out8(bus, BUSMASTER_REG_CMD, cmdvalue);
    busmaster_out8(bus, BUSMASTER_REG_STATUS, 0x06);
    return 0;
}

[[nodiscard]] static int atadisk_op_dma_begin_transfer(struct Ata_Disk *self) {
    struct disk *disk = self->data;
    struct bus *bus = disk->bus;
    uint8_t bmreg = busmaster_in8(bus, BUSMASTER_REG_CMD);
    bmreg |= BUSMASTER_CMDFLAG_START;
    busmaster_out8(bus, BUSMASTER_REG_CMD, bmreg);
    return 0;
}

static ATA_DMASTATUS atadisk_op_dma_check_transfer(struct Ata_Disk *self) {
    struct disk *disk = self->data;
    struct bus *bus = disk->bus;
    /* We have to read the status after IRQ. */
    uint8_t bmstatus = busmaster_in8(bus, BUSMASTER_REG_STATUS);
    if (bmstatus & (1U << 1)) {
        uint16_t pcistatus = Pci_ReadStatusReg(bus->pcipath);
        drive_printf(bus, disk->driveid, "DMA error occured. busmaster status %02x, PCI status %04x\n", bmstatus, pcistatus);
        Pci_WriteStatusReg(bus->pcipath, PCI_STATUSFLAG_MASTER_DATA_PARITY_ERROR | PCI_STATUSFLAG_RECEIVED_TARGET_ABORT | PCI_STATUSFLAG_RECEIVED_MASTER_ABORT);
        if (pcistatus & PCI_STATUSFLAG_MASTER_DATA_PARITY_ERROR) {
            return ATA_DMASTATUS_FAIL_UDMA_CRC;
        }
        return ATA_DMASTATUS_FAIL_OTHER_IO;
    }
    if (!(bmstatus & (1U << 0))) {
        Pci_WriteStatusReg(bus->pcipath, PCI_STATUSFLAG_MASTER_DATA_PARITY_ERROR | PCI_STATUSFLAG_RECEIVED_TARGET_ABORT | PCI_STATUSFLAG_RECEIVED_MASTER_ABORT);
        return ATA_DMASTATUS_SUCCESS;
    }

    return ATA_DMASTATUS_BUSY;
}

static void atadisk_op_dma_end_transfer(struct Ata_Disk *self, bool wassuccess) {
    struct disk *disk = self->data;
    struct bus *bus = disk->bus;
    busmaster_out8(bus, BUSMASTER_REG_CMD, busmaster_in8(bus, BUSMASTER_REG_CMD) & ~BUSMASTER_CMDFLAG_START);
    if (bus->is_dma_read && wassuccess) {
        for (size_t i = 0; i < bus->prd_count; i++) {
            size_t size = bus->prdt[i].len;
            if (size == 0) {
                size = 65536;
            }
            PMemCopyIn(&((uint8_t *)bus->dma_buffer)[i * MAX_TRANSFTER_SIZE_PER_PRD], bus->prdt[i].buffer_physaddr, size, true);
            if (bus->prdt[i].flags & PRD_FLAG_LAST_ENTRY_IN_PRDT) {
                break;
            }
        }
    }
}

static void atadisk_op_dma_deinit_transfer(struct Ata_Disk *self) {
    struct disk *disk = self->data;
    struct bus *bus = disk->bus;
    assert(bus->busmaster_enabled);
}

static struct Ata_DiskOps const OPS = {
    .Dma_BeginSession = atadisk_op_dma_begin_session,
    .Dma_EndSession = atadisk_op_dma_end_session,
    .Lock = atadisk_op_lock,
    .Unlock = atadisk_op_unlock,
    .ReadStatus = atadisk_op_read_status,
    .SelectDisk = atadisk_op_select_disk,
    .SetFeaturesParam = atadisk_op_set_features_param,
    .SetCountParam = atadisk_op_set_count_param,
    .SetLbaParam = atadisk_op_set_lba_param,
    .SetDeviceParam = atadisk_op_set_device_param,
    .GetLbaOutput = atadisk_op_get_lba_output,
    .IssueCommand = atadisk_op_issue_cmd,
    .GetIrqFlag = atadisk_op_get_irq_flag,
    .ClearIrqFlag = atadisk_op_clear_irq_flag,
    .ReadData = atadisk_op_read_data,
    .WriteData = atadisk_op_write_data,
    .Dma_InitTransfer = atadisk_op_dma_init_transfter,
    .Dma_BeginTransfer = atadisk_op_dma_begin_transfer,
    .Dma_CheckTransfer = atadisk_op_dma_check_transfer,
    .Dma_EndTransfer = atadisk_op_dma_end_transfer,
    .Dma_DeinitTransfer = atadisk_op_dma_deinit_transfer,
    .SoftReset = atadisk_op_soft_reset,
};

static void irq_handler(int irqnum, void *data) {
    struct bus *bus = data;
    bus->got_irq = true;
    io_in8(bus, IOREG_STATUS);
    ArchI586_Pic_SendEoi(irqnum);
}

static bool init_busmaster(struct bus *bus) {
    /* This should be enough to store allocated page counts. */
    size_t page_counts[(MAX_DMA_TRANSFER_SIZE_NEEDED / MAX_TRANSFTER_SIZE_PER_PRD) + 1];

    /* Allocate resources needed for busmastering DMA ************************/
    bus->prd_count = SizeToBlocks(MAX_DMA_TRANSFER_SIZE_NEEDED, MAX_TRANSFTER_SIZE_PER_PRD);
    size_t prdtsize = bus->prd_count * sizeof(*bus->prdt);
    assert(prdtsize < MAX_TRANSFTER_SIZE_PER_PRD);
    size_t prdt_page_count = SizeToBlocks(prdtsize, ARCH_PAGESIZE);
    bool phys_alloc_ok = false;
    struct Vmm_Object *prdt_vm_object = NULL;
    size_t allocated_prdt_count = 0;
    bus->prdt_physbase = Pmm_Alloc(&prdt_page_count);
    if (bus->prdt_physbase == PHYSICALPTR_NULL) {
        goto fail_oom;
    }
    phys_alloc_ok = true;
    prdt_vm_object = Vmm_MapMemory(Vmm_GetKernelAddressSpace(), bus->prdt_physbase, prdt_page_count * ARCH_PAGESIZE, MAP_PROT_READ | MAP_PROT_WRITE | MAP_PROT_NOCACHE);
    if (prdt_vm_object == NULL) {
        bus_printf(bus, "not enough memory for busmaster PRDT\n");
        goto fail_oom;
    }
    /* Fill PRDT **************************************************************/
    bus->prdt = prdt_vm_object->start;
    memset(bus->prdt, 0, prdtsize);
    size_t remaining_size = MAX_DMA_TRANSFER_SIZE_NEEDED;
    for (size_t i = 0; i < bus->prd_count; i++) {
        size_t current_size = remaining_size;
        if (MAX_TRANSFTER_SIZE_PER_PRD < current_size) {
            current_size = MAX_TRANSFTER_SIZE_PER_PRD;
        }
        /* NOTE: We setup PRD's len and flags when we initialize DMA transfer */
        size_t current_page_count = SizeToBlocks(current_size, ARCH_PAGESIZE);
        bus->prdt[i].buffer_physaddr = Pmm_Alloc(&current_page_count);
        if (bus->prdt[i].buffer_physaddr == PHYSICALPTR_NULL) {
            goto fail_oom;
        }
        page_counts[i] = current_page_count;
        PMemSet(bus->prdt[i].buffer_physaddr, 0x00, current_size, true);
        allocated_prdt_count++;
        remaining_size -= current_size;
    }
    return true;
fail_oom:
    bus_printf(bus, "not enough memory for busmaster PRDT. falling back to PIO-only.\n");
    for (size_t i = 0; i < allocated_prdt_count; i++) {
        Pmm_Free(bus->prdt[i].buffer_physaddr, page_counts[i]);
    }
    if (prdt_vm_object != NULL) {
        Vmm_Free(prdt_vm_object);
    }
    if (phys_alloc_ok) {
        Pmm_Free(bus->prdt_physbase, prdt_page_count);
    }
    return false;
}

static int init_controller(struct shared *shared, uint16_t io_base, uint16_t ctrl_base, uint16_t busmastrer_base, PCIPATH pcipath, bool busmaster_enabled, uint8_t irq, size_t channel_index) {
    int result = 0;
    struct bus *bus = Heap_Alloc(sizeof(*bus), HEAP_FLAG_ZEROMEMORY);
    if (bus == NULL) {
        result = -ENOMEM;
        goto fail;
    }
    bus->shared = shared;
    bus->pcipath = pcipath;
    bus->io_iobase = io_base;
    bus->ctrl_iobase = ctrl_base;
    bus->busmastrer_iobase = busmastrer_base;
    bus->last_selected_drive = -1;

    if (busmaster_enabled) {
        bus->busmaster_enabled = init_busmaster(bus);
    }
    uint8_t busstatus = read_status(bus);
    if ((busstatus & 0x7f) == 0x7f) {
        bus_printf(bus,
                   "seems to be floating(got status byte %#x)\n", busstatus);
        result = -EIO;
        goto fail;
    }
    /* Prepare to receive IRQs ************************************************/
    ArchI586_Pic_RegisterHandler(&bus->irq_handler, irq, irq_handler, bus);
    shared->buses[channel_index] = bus;
    ArchI586_Pic_UnmaskIrq(irq);
    reset_bus(bus);
    /* Some systems seem to fire IRQ after reset. *****************************/
    for (uint8_t i = 0; i < 255; i++) {
        if (bus->got_irq) {
            bus->got_irq = false;
            break;
        }
        Arch_IoDelay();
    }
    bus_printf(bus, "probing the bus\n");
    for (int8_t drive = 0; drive < 2; drive++) {
        select_drive(bus, drive);
        struct disk *disk = Heap_Alloc(sizeof(*disk), HEAP_FLAG_ZEROMEMORY);
        if (disk == NULL) {
            continue;
        }
        disk->bus = bus;
        disk->driveid = drive;
        int ret = AtaDisk_Register(&disk->atadisk, &OPS, disk);
        if (ret < 0) {
            if (ret == -ENODEV) {
                drive_printf(bus, drive, "nothing there or non-accessible\n");
            } else {
                drive_printf(bus, drive, "failed to initialize disk (error %d)\n", ret);
            }
            Heap_Free(disk);
            continue;
        }
        drive_printf(bus, drive, "disk registered\n");
    }
    bus_printf(bus, "bus probing complete\n");
    goto out;
fail:
    Heap_Free(bus);
out:
    return result;
}

/*
 * Each channel can be either in native or compatibility mode(~_NATIVE flag set means it's in native mode),
 * and ~_SWITCHABLE flag means whether it is possible to switch between two modes.
 */
#define PROGIF_FLAG_CHANNEL0_MODE_NATIVE (1U << 0)
#define PROGIF_FLAG_CHANNEL0_MODE_SWITCHABLE (1U << 1)
#define PROGIF_FLAG_CHANNEL1_MODE_NATIVE (1U << 2)
#define PROGIF_FLAG_CHANNEL1_MODE_SWITCHABLE (1U << 3)
#define PROGIF_FLAG_BUSMASTER_SUPPORTED (1U << 7)

static void reprogram_progif(PCIPATH path, uint8_t progif) {
    uint8_t new_progif = progif;
    if (!(progif & PROGIF_FLAG_CHANNEL0_MODE_NATIVE) && (progif & PROGIF_FLAG_CHANNEL0_MODE_SWITCHABLE)) {
        new_progif |= PROGIF_FLAG_CHANNEL0_MODE_NATIVE;
    }
    if (!(progif & PROGIF_FLAG_CHANNEL1_MODE_NATIVE) && (progif & PROGIF_FLAG_CHANNEL1_MODE_SWITCHABLE)) {
        new_progif |= PROGIF_FLAG_CHANNEL1_MODE_NATIVE;
    }
    if (progif != new_progif) {
        Pci_Printf(path, "idebus: reprogramming Prog IF value: %02x -> %02x\n", progif, new_progif);
        Pci_WriteProgIF(path, new_progif);
        progif = Pci_ReadProgIF(path);
        if (progif != new_progif) {
            Pci_Printf(path, "idebus: failed to reprogram Prog IF - Using the value as-is\n");
        }
    }
}

static int read_channel_bar(uint16_t *io_base_out, uint16_t *ctrl_base_out, uint8_t *irq_out, PCIPATH path, int io_bar, int ctrl_bar, int chan) {
    uintptr_t io_base = 0;
    uintptr_t ctrl_base = 0;
    uint8_t irq = 0;
    irq = Pci_ReadIrqLine(path);
    int ret = Pci_ReadIoBar(&io_base, path, io_bar);
    if (ret < 0) {
        goto out;
    }
    ret = Pci_ReadIoBar(&ctrl_base, path, ctrl_bar);
    if (ret < 0) {
        goto out;
    }
    /* Only the port at offset 2 is the real control port. ********************/
    ctrl_base += 2;
    ret = 0;
    Pci_Printf(path, "idebus: [channel%d] I/O base %#x, control base %#x, IRQ %d\n", chan, io_base, ctrl_base, irq);
out:
    if (ret < 0) {
        Pci_Printf(path, "idebus: could not read one of BARs for channel%d\n", chan);
    }
    *io_base_out = io_base;
    *ctrl_base_out = ctrl_base;
    *irq_out = irq;
    return ret;
}

static int read_busmaster_bar(uint16_t *base_out, PCIPATH path) {
    uintptr_t base = 0;
    int ret = Pci_ReadIoBar(&base, path, 4);
    if (ret < 0) {
        goto out;
    }
    ret = 0;
    Pci_Printf(path, "idebus: [busmaster] base %#x\n", base);
out:
    if (ret < 0) {
        Pci_Printf(path, "idebus: could not read the BAR for bus mastering");
    }
    *base_out = base;
    return ret;
}

static void pci_probe_callback(PCIPATH path, uint16_t venid, uint16_t devid, uint8_t baseclass, uint8_t subclass, void *data) {
    (void)venid;
    (void)devid;
    (void)data;
    if ((baseclass != 0x1) || (subclass != 0x1)) {
        return;
    }
    uint8_t progif = Pci_ReadProgIF(path);
    uint8_t channel0_irq = 14;
    uint8_t channel1_irq = 15;
    uint16_t channel0_io_base = 0x1F0;
    uint16_t channel0_ctrl_base = 0x3F6;
    uint16_t channel1_io_base = 0x170;
    uint16_t channel1_ctrl_base = 0x376;
    uint16_t busmaster_io_base = 0;
    bool channel0_enabled = true;
    bool channel1_enabled = true;
    bool busmaster_enabled = true;

    uint16_t pcicmd = Pci_ReadCmdReg(path);
    pcicmd |= PCI_CMDFLAG_IO_SPACE | PCI_CMDFLAG_MEMORY_SPACE | PCI_CMDFLAG_BUS_MASTER;
    Pci_WriteCmdReg(path, pcicmd);

    if (CONFIG_REPROGRAM_PROGIF) {
        reprogram_progif(path, progif);
    }
    if (progif & PROGIF_FLAG_CHANNEL0_MODE_NATIVE) {
        int ret = read_channel_bar(&channel0_io_base, &channel0_ctrl_base, &channel0_irq, path, 0, 1, 0);
        if (ret < 0) {
            channel0_enabled = false;
        }
    }
    if (progif & PROGIF_FLAG_CHANNEL1_MODE_NATIVE) {
        int ret = read_channel_bar(&channel1_io_base, &channel1_ctrl_base, &channel1_irq, path, 2, 3, 1);
        if (ret < 0) {
            channel1_enabled = false;
        }
    }
    if (progif & PROGIF_FLAG_BUSMASTER_SUPPORTED) {
        int ret = read_busmaster_bar(&busmaster_io_base, path);
        if (ret < 0) {
            busmaster_enabled = false;
        }
    }
    struct shared *shared = Heap_Alloc(sizeof(*shared), HEAP_FLAG_ZEROMEMORY);
    if (shared == NULL) {
        Pci_Printf(path, "idebus: not enough memory\n");
        return;
    }
    shared->dma_lock_flag = false;
    /*
     * If Simplex Only is set, we need DMA lock to prevent both channels using
     * DMA at the same time.
     */
    uint8_t bm_status = ArchI586_In8(busmaster_io_base + BUSMASTER_REG_STATUS);
    shared->dma_lock_needed = bm_status & (1U << 7);
    if (channel0_enabled) {
        int ret = init_controller(
            shared, channel0_io_base, channel0_ctrl_base,
            busmaster_io_base, path, busmaster_enabled,
            channel0_irq, 0);
        if (ret < 0) {
            Pci_Printf(path, "idebus: [channel0] failed to initialize (error %d)\n", ret);
        }
    }
    if (channel1_enabled) {
        int ret = init_controller(
            shared, channel1_io_base, channel1_ctrl_base,
            busmaster_io_base + 8, path,
            busmaster_enabled, channel1_irq, 1);
        if (ret < 0) {
            Pci_Printf(path, "idebus: [channel1] failed to initialize (error %d)\n", ret);
        }
    }
}

void ArchI586_IdeBus_Init(void) {
    Pci_ProbeBus(pci_probe_callback, NULL);
}
