#include "../ioport.h"
#include "../pic.h"
#include "idebus.h"
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

//------------------------------- Configuration -------------------------------

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

//-----------------------------------------------------------------------------


// Physical Region Descriptor
struct prd {
    uint32_t buffer_physaddr;
    uint16_t len;   // 0 = 64K
    uint16_t flags; /*
                     * All bits are reserved(should be 0) except for top bit
                     * (PRD_FLAG_LAST_ENTRY_IN_PRDT)
                     */
};

// shared data between two IDE buses in a controller
struct shared {
    struct bus *buses[2];
    _Atomic bool dma_lock_flag;
    bool dma_lock_needed : 1;
};

struct bus {
    struct archi586_pic_irq_handler irq_handler;
    physptr prdt_physbase;
    struct prd *prdt;
    struct shared *shared;
    size_t prd_count;
    void *dma_buffer;
    uint16_t io_iobase;
    uint16_t ctrl_iobase;
    uint16_t busmastrer_iobase;
    pcipath pcipath;
    int8_t last_selected_drive; // -1: No device was selected before
    _Atomic bool bus_lock_flag;
    _Atomic bool got_irq;
    bool is_dma_read       : 1;
    bool busmaster_enabled : 1;
};

struct disk {
    struct atadisk atadisk;
    int8_t driveid;
    struct bus *bus;
};

enum ioreg {
    IOREG_DATA,
    IOREG_ERROR          = 1, // read
    IOREG_FEATURES       = 1, // write
    IOREG_SECTORCOUNT    = 2,
    IOREG_LBA_LO         = 3,
    IOREG_LBA_MID        = 4,
    IOREG_LBA_HI         = 5,
    IOREG_DRIVE_AND_HEAD = 6,
    IOREG_STATUS         = 7, // read
    IOREG_COMMAND        = 7, // write
};

#define DRIVE_AND_HEAD_FLAG_DRV     (1U << 4)
#define DRIVE_AND_HEAD_FLAG_LBA     (1U << 6)

static void io_out8(struct bus *self, enum ioreg reg, uint8_t data) {
    archi586_out8(self->io_iobase + reg, data);
}
static void io_out16(struct bus *self, enum ioreg reg, uint16_t data) {
    archi586_out16(self->io_iobase + reg, data);
}
static uint8_t io_in8(struct bus *self, enum ioreg reg) {
    return archi586_in8(self->io_iobase + reg);
}
static uint16_t io_in16(struct bus *self, enum ioreg reg) {
    return archi586_in16(self->io_iobase + reg);
}
static void io_in16_rep(
    struct bus *self, enum ioreg reg, void *buf, size_t len)
{
    archi586_in16_rep(self->io_iobase + reg, buf, len);
}

enum ctrlreg {
    CTRLREG_ALTERNATE_STATUS = 0, // read
    CTRLREG_DEVICE_CONTROL   = 0, // write
};

#define DRVICE_CONTROL_FLAG_NIEN    (1U << 1)
#define DRVICE_CONTROL_FLAG_SRST    (1U << 2)
#define DRVICE_CONTROL_FLAG_HOB     (1U << 7)

static void ctrl_out8(struct bus *self, enum ctrlreg reg, uint8_t data) {
    archi586_out8(self->ctrl_iobase + reg, data);
}

static uint8_t ctrl_in8(struct bus *self, enum ctrlreg reg) {
    return archi586_in8(self->ctrl_iobase + reg);
}

static uint8_t read_status(struct bus *self) {
    return ctrl_in8(self, CTRLREG_ALTERNATE_STATUS);
}

static void bus_printf(struct bus *self, char const *fmt, ...) {
    va_list ap;

    co_printf("idebus(%x): ", self->io_iobase);
    va_start(ap, fmt);
    co_vprintf(fmt, ap);
    va_end(ap);
}

static void drive_printf(struct bus *self, int drive, char const *fmt, ...) {
    va_list ap;

    co_printf("idebus(%x)drive(%d): ", self->io_iobase, drive);
    va_start(ap, fmt);
    co_vprintf(fmt, ap);
    va_end(ap);
}

static void reset_bus(struct bus *self) {
    uint8_t regval = ctrl_in8(self, CTRLREG_DEVICE_CONTROL);
    regval &= ~(DRVICE_CONTROL_FLAG_NIEN | DRVICE_CONTROL_FLAG_HOB);
    regval |= DRVICE_CONTROL_FLAG_SRST;
    ctrl_out8(self, CTRLREG_DEVICE_CONTROL, regval);
    arch_iodelay();
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

static void atadisk_op_soft_reset(struct atadisk *self) {
    struct disk *disk = self->data;
    reset_bus(disk->bus);
}

static void atadisk_op_select_disk(struct atadisk *self) {
    struct disk *disk = self->data;
    select_drive(disk->bus, disk->driveid);
}
static uint8_t atadisk_op_read_status(struct atadisk *self) {
    struct disk *disk = self->data;
    return read_status(disk->bus);
}
static void atadisk_op_set_features_param(struct atadisk *self, uint16_t data) {
    struct disk *disk = self->data;
    io_out8(disk->bus, IOREG_FEATURES, data);
}
static void atadisk_op_set_count_param(struct atadisk *self, uint16_t data) {
    struct disk *disk = self->data;
    io_out8(disk->bus, IOREG_SECTORCOUNT, data);
}
static void atadisk_op_set_lba_param(struct atadisk *self, uint32_t data) {
    struct disk *disk = self->data;
    io_out8(disk->bus, IOREG_LBA_LO, data);
    io_out8(disk->bus, IOREG_LBA_MID, data >> 8);
    io_out8(disk->bus, IOREG_LBA_HI, data >> 16);
    uint8_t regvalue = io_in8(disk->bus, IOREG_DRIVE_AND_HEAD);
    regvalue = (regvalue & ~0x0fU) | ((data >> 24) & 0x0fU);
    io_out8(disk->bus, IOREG_DRIVE_AND_HEAD, regvalue);
}
static void atadisk_op_set_device_param(struct atadisk *self, uint8_t data) {
    struct disk *disk = self->data;
    uint8_t regvalue = io_in8(disk->bus, IOREG_DRIVE_AND_HEAD);
    // Note that we preserve lower 3-bit, which contains upper 4-bit of LBA.
    // (ACS-3 calls these bits as "reserved", and maybe this is the reason?)
    regvalue = (data & ~0x0fU) | (regvalue & 0x0fU);
    io_out8(disk->bus, IOREG_DRIVE_AND_HEAD, regvalue);
}
static uint32_t atadisk_op_get_lba_output(struct atadisk *self) {
    struct disk *disk = self->data;
    uint32_t lba_lo  = io_in8(disk->bus, IOREG_LBA_LO);
    uint32_t lba_mid = io_in8(disk->bus, IOREG_LBA_MID);
    uint32_t lba_hi  = io_in8(disk->bus, IOREG_LBA_HI);
    uint32_t lba_top4bit =
        io_in8(disk->bus, IOREG_DRIVE_AND_HEAD) & 0x0F;
    return (lba_top4bit << 24) | (lba_hi << 16) | (lba_mid << 8) | lba_lo;
}

static void atadisk_op_issue_cmd(struct atadisk *self, enum ata_cmd cmd) {
    struct disk *disk = self->data;
    io_out8(disk->bus, IOREG_COMMAND, cmd);
}
static bool atadisk_op_get_irq_flag(struct atadisk *self) {
    struct disk *disk = self->data;
    return disk->bus->got_irq;
}
static void atadisk_op_clear_irq_flag(struct atadisk *self) {
    struct disk *disk = self->data;
    disk->bus->got_irq = false;
}
static void atadisk_op_read_data(
    struct ata_databuf *out, struct atadisk *self)
{
    struct disk *disk = self->data;
    io_in16_rep(disk->bus, IOREG_DATA, out->data, sizeof(out->data)/sizeof(*out->data));
}
static void atadisk_op_write_data(
    struct atadisk *self, struct ata_databuf *buffer)
{
    struct disk *disk = self->data;
    for (size_t i = 0; i < 256; i++) {
        io_out16(disk->bus, IOREG_DATA, buffer->data[i]);
        arch_iodelay();
    }
}

enum busmaster_reg {
    BUSMASTERREG_CMD  = 0,
    BUSMASTERREG_STATUS   = 2,
    BUSMASTERREG_PRDTADDR = 4,
}; 

static void busmaster_out8(struct bus *self, enum busmaster_reg reg, uint8_t data) {
    assert(self->busmaster_enabled);
    archi586_out8(self->busmastrer_iobase + reg, data);
}
static void busmaster_out32(struct bus *self, enum busmaster_reg reg, uint32_t data) {
    assert(self->busmaster_enabled);
    archi586_out32(self->busmastrer_iobase + reg, data);
}
static uint8_t busmaster_in8(struct bus *self, enum busmaster_reg reg) {
    assert(self->busmaster_enabled);
    return archi586_in8(self->busmastrer_iobase + reg);
}
static uint32_t busmaster_in32(struct bus *self, enum busmaster_reg reg) {
    assert(self->busmaster_enabled);
    return archi586_in32(self->busmastrer_iobase + reg);
}

#define BUSMASTER_CMDFLAG_START     (1U << 0)
#define BUSMASTER_CMDFLAG_READ      (1U << 3)

enum {
    MAX_TRANSFTER_SIZE_PER_PRD   = 65536,
    MAX_DMA_TRANSFER_SIZE_NEEDED = ATA_MAX_SECTORS_PER_TRANSFER * ATA_SECTOR_SIZE
};

static bool atadisk_op_dma_beginsession(struct atadisk *self) {
    struct disk *disk = self->data;
    struct bus *bus = disk->bus;
    if (!bus->busmaster_enabled) {
        // No DMA support
        return false;
    }
    if (!bus->shared->dma_lock_needed) {
        // No DMA lock is used - We are good to go.
        return true;
    }
    // If DMA lock is present, that means both IDE channels can't use DMA at the same time.
    // So we must lock the DMA, and if we can't, we have to fall back to PIO.
    bool expected = false;
    bool desired = true;
    if (!atomic_compare_exchange_strong_explicit(&bus->shared->dma_lock_flag, &expected, desired, memory_order_acquire, memory_order_relaxed)) {
        // Someone else is using DMA
        return false;
    }
    return true;
}

static void atadisk_op_dma_endsession(struct atadisk *self) {
    struct disk *disk = self->data;
    struct bus *bus = disk->bus;
    assert(bus->busmaster_enabled);
    if (!bus->shared->dma_lock_needed) {
        return;
    }
    bool desired = false;
    atomic_store_explicit(&bus->shared->dma_lock_flag, desired, memory_order_release);
}

static void atadisk_op_lock(struct atadisk *self) {
    struct disk *disk = self->data;
    struct bus *bus = disk->bus;
    bool expected = false;
    bool desired = true;
    while (!atomic_compare_exchange_strong_explicit(&bus->bus_lock_flag, &expected, desired, memory_order_acquire, memory_order_relaxed)) {}
}

static void atadisk_op_unlock(struct atadisk *self) {
    struct disk *disk = self->data;
    struct bus *bus = disk->bus;
    bool desired = false;
    atomic_store_explicit(&bus->bus_lock_flag, desired, memory_order_release);
}

WARN_UNUSED_RESULT static int atadisk_op_dma_init_transfter(
    struct atadisk *self, void *buffer, size_t len, bool isread)
{
    struct disk *disk = self->data;
    struct bus *bus = disk->bus;
    assert(bus->busmaster_enabled);
    assert(len <= MAX_DMA_TRANSFER_SIZE_NEEDED);
    bus->dma_buffer = buffer;
    bus->is_dma_read = isread;
    size_t remainingsize = len;
    for (size_t i = 0; remainingsize != 0; i++) {
        assert(i < bus->prd_count);
        size_t currentsize = remainingsize;
        if (MAX_TRANSFTER_SIZE_PER_PRD < currentsize) {
            currentsize = MAX_TRANSFTER_SIZE_PER_PRD;
        }
        if (currentsize == MAX_TRANSFTER_SIZE_PER_PRD) {
            bus->prdt[i].len = 0;
        } else {
            bus->prdt[i].len = currentsize;
        }
        if (remainingsize <= MAX_TRANSFTER_SIZE_PER_PRD) {
            // This should be the last PRD
            bus->prdt[i].flags |= PRD_FLAG_LAST_ENTRY_IN_PRDT;
        } else {
            bus->prdt[i].flags = 0;
        }
        if (!isread) {
            pmemcpy_out(
                bus->prdt[i].buffer_physaddr,
                &((uint8_t *)buffer)[i * MAX_TRANSFTER_SIZE_PER_PRD],
                currentsize, true);
        }
        remainingsize -= currentsize;
    }
    // Setup busmaster registers
    busmaster_out32(
        bus, BUSMASTERREG_PRDTADDR, bus->prdt_physbase);
    uint8_t cmdvalue = 0;
    if (isread) {
        cmdvalue |= BUSMASTER_CMDFLAG_READ;
    } else {
        cmdvalue &= ~BUSMASTER_CMDFLAG_READ;
    }
    busmaster_out8(bus, BUSMASTERREG_CMD, cmdvalue);
    busmaster_out8(bus, BUSMASTERREG_STATUS, 0x06);
    return 0;
}

WARN_UNUSED_RESULT static int atadisk_op_dma_begin_transfer(
    struct atadisk *self) {
    struct disk *disk = self->data;
    struct bus *bus = disk->bus;
    uint8_t bmreg = busmaster_in8(bus, BUSMASTERREG_CMD);
    bmreg |= BUSMASTER_CMDFLAG_START;
    busmaster_out8(bus, BUSMASTERREG_CMD, bmreg);
    return 0;
}

static enum ata_dmastatus atadisk_op_dma_check_transfer(struct atadisk *self) {
    struct disk *disk = self->data;
    struct bus *bus = disk->bus;
     // We have to read the status after IRQ.
    uint8_t bmstatus = busmaster_in8(bus, BUSMASTERREG_STATUS);
    if (bmstatus & (1U << 1)) {
        uint16_t pcistatus = pci_readstatusreg(bus->pcipath);
        drive_printf(
            bus, disk->driveid,
            "DMA error occured. busmaster status %02x, PCI status %04x\n",
            bmstatus, pcistatus);
        pci_writestatusreg(bus->pcipath, PCI_STATUSFLAG_MASTER_DATA_PARITY_ERROR | PCI_STATUSFLAG_RECEIVED_TARGET_ABORT | PCI_STATUSFLAG_RECEIVED_MASTER_ABORT);
        if (pcistatus & PCI_STATUSFLAG_MASTER_DATA_PARITY_ERROR) {
            return ATA_DMASTATUS_FAIL_UDMA_CRC;
        }
        return ATA_DMASTATUS_FAIL_OTHER_IO;
    }
    if (!(bmstatus & (1U << 0))) {
        pci_writestatusreg(bus->pcipath, PCI_STATUSFLAG_MASTER_DATA_PARITY_ERROR | PCI_STATUSFLAG_RECEIVED_TARGET_ABORT | PCI_STATUSFLAG_RECEIVED_MASTER_ABORT);
        return ATA_DMASTATUS_SUCCESS;
    }

    return ATA_DMASTATUS_BUSY;
}

static void atadisk_op_dma_end_transfer(struct atadisk *self, bool wassuccess) {
    struct disk *disk = self->data;
    struct bus *bus = disk->bus;
    busmaster_out8(bus, BUSMASTERREG_CMD, busmaster_in8(bus, BUSMASTERREG_CMD) & ~BUSMASTER_CMDFLAG_START);
    if (bus->is_dma_read && wassuccess) {
        for (size_t i = 0; i < bus->prd_count; i++) {
            size_t size = bus->prdt[i].len;
            if (size == 0) {
                size = 65536;
            }
            pmemcpy_in(&((uint8_t *)bus->dma_buffer)[i * MAX_TRANSFTER_SIZE_PER_PRD], bus->prdt[i].buffer_physaddr, size, true);
            if (bus->prdt[i].flags & PRD_FLAG_LAST_ENTRY_IN_PRDT) {
                break;
            }
        }
    }
}

static void atadisk_op_dma_deinit_transfer(struct atadisk *self) {
    struct disk *disk = self->data;
    struct bus *bus = disk->bus;
    assert(bus->busmaster_enabled);
}

static struct atadisk_ops const OPS = {
    .dma_beginsession    = atadisk_op_dma_beginsession,
    .dma_endsession      = atadisk_op_dma_endsession,
    .lock                = atadisk_op_lock,
    .unlock              = atadisk_op_unlock,
    .read_status         = atadisk_op_read_status,
    .select_disk         = atadisk_op_select_disk,
    .set_features_param  = atadisk_op_set_features_param,
    .set_count_param     = atadisk_op_set_count_param,
    .set_lba_param       = atadisk_op_set_lba_param,
    .set_device_param    = atadisk_op_set_device_param,
    .get_lba_output      = atadisk_op_get_lba_output,
    .issue_cmd           = atadisk_op_issue_cmd,
    .get_irq_flag        = atadisk_op_get_irq_flag,
    .clear_irq_flag      = atadisk_op_clear_irq_flag,
    .read_data           = atadisk_op_read_data,
    .write_data          = atadisk_op_write_data,
    .dma_init_transfer   = atadisk_op_dma_init_transfter,
    .dma_begin_transfer  = atadisk_op_dma_begin_transfer,
    .dma_check_transfer  = atadisk_op_dma_check_transfer,
    .dma_end_transfer    = atadisk_op_dma_end_transfer,
    .dma_deinit_transfer = atadisk_op_dma_deinit_transfer,
    .soft_reset          = atadisk_op_soft_reset,
};

static void irq_handler(int irqnum,  void *data) {
    struct bus *bus = data;
    bus->got_irq = true;
    io_in8(bus, IOREG_STATUS);
    archi586_pic_sendeoi(irqnum);
}

static bool init_busmaster(struct bus *bus) {
    // This should be enough to store allocated page counts.
    size_t page_counts[
        (MAX_DMA_TRANSFER_SIZE_NEEDED / MAX_TRANSFTER_SIZE_PER_PRD) + 1];
    // Allocate resources needed for busmaster_ing DMA
    bus->prd_count = size_to_blocks(
        MAX_DMA_TRANSFER_SIZE_NEEDED,
        MAX_TRANSFTER_SIZE_PER_PRD);
    size_t prdtsize = bus->prd_count * sizeof(*bus->prdt);
    assert(prdtsize < MAX_TRANSFTER_SIZE_PER_PRD);
    size_t prdtpagecount = size_to_blocks(
        prdtsize, ARCH_PAGESIZE);
    bool physallocok = false;
    struct vmobject *prdtvmobject = NULL;
    size_t allocated_prdt_count = 0;
    bus->prdt_physbase = pmm_alloc( &prdtpagecount);
    if (bus->prdt_physbase == PHYSICALPTR_NULL) {
        goto fail_oom;
    }
    physallocok = true;
    prdtvmobject = vmm_map(
        vmm_get_kernel_addressspace(),
        bus->prdt_physbase,
        prdtpagecount * ARCH_PAGESIZE,
        MAP_PROT_READ | MAP_PROT_WRITE | MAP_PROT_NOCACHE
    );
    if (prdtvmobject == NULL) {
        bus_printf(bus, "not enough memory for busmaster PRDT\n");
        goto fail_oom;
    }
    bus->prdt = prdtvmobject->start;
    memset(bus->prdt, 0, prdtsize);
    size_t remainingsize = MAX_DMA_TRANSFER_SIZE_NEEDED;
    for (size_t i = 0; i < bus->prd_count; i++) {
        size_t currentsize = remainingsize;
        if (MAX_TRANSFTER_SIZE_PER_PRD < currentsize) {
            currentsize = MAX_TRANSFTER_SIZE_PER_PRD;
        }
        /*
         * NOTE: We setup PRD's len and flags when we initialize DMA
         * transfer
         */
        size_t currentpagecount =
            size_to_blocks(currentsize, ARCH_PAGESIZE);
        bus->prdt[i].buffer_physaddr =
            pmm_alloc(&currentpagecount);
        if (bus->prdt[i].buffer_physaddr == PHYSICALPTR_NULL) {
            goto fail_oom;
        }
        page_counts[i] = currentpagecount;
        pmemset(
            bus->prdt[i].buffer_physaddr, 0x00, currentsize,
            true);
        allocated_prdt_count++;
        remainingsize -= currentsize;
    }
    return true;
fail_oom:
    bus_printf(
        bus,
        "not enough memory for busmaster PRDT. falling back to PIO-only.\n");
    for (size_t i = 0; i < allocated_prdt_count; i++) {
        pmm_free(
            bus->prdt[i].buffer_physaddr, page_counts[i]);
    }
    if (prdtvmobject != NULL) {
        vmm_free(prdtvmobject);
    }
    if (physallocok) {
        pmm_free(bus->prdt_physbase, prdtpagecount);
    }
    return false;
}

static int init_controller(
    struct shared *shared, uint16_t io_base, uint16_t ctrl_base,
    uint16_t busmastrer_base, pcipath pcipath, bool busmaster_enabled,
    uint8_t irq, size_t channel_index)
{
    int result = 0;
    struct bus *bus = heap_alloc(
        sizeof(*bus), HEAP_FLAG_ZEROMEMORY);
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
    // Prepare to receive IRQs
    archi586_pic_registerhandler(&bus->irq_handler, irq, irq_handler, bus);
    shared->buses[channel_index] = bus;
    archi586_pic_unmaskirq(irq);
    reset_bus(bus);
    // Some systems seem to fire IRQ after reset.
    for (uint8_t i = 0; i < 255; i++) {
        if (bus->got_irq) {
            bus->got_irq = false;
            break;
        }
        arch_iodelay();
    }
    bus_printf(bus, "probing the bus\n");
    for (int8_t drive = 0; drive < 2; drive++) {
        select_drive(bus, drive);
        struct disk *disk = heap_alloc(sizeof(*disk), HEAP_FLAG_ZEROMEMORY);
        if (disk == NULL) {
            continue;
        }
        disk->bus = bus;
        disk->driveid = drive;
        int ret = atadisk_register(
            &disk->atadisk, &OPS, disk);
        if (ret < 0) {
            if (ret == -ENODEV) {
                drive_printf(bus, drive,
                    "nothing there or non-accessible\n");
            } else {
                drive_printf(
                    bus, drive, "failed to initialize disk (error %d)\n", ret);
            }
            heap_free(disk);
            continue;
        }
        drive_printf(bus, drive, "disk registered\n");
    }
    bus_printf(bus, "bus probing complete\n");
    goto out;
fail:
    heap_free(bus);
out:
    return result;
}

/*
 * Each channel can be either in native or compatibility mode(~_NATIVE
 * flag set means it's in native mode), and ~_SWITCHABLE flag means
 * whether it is possible to switch between
 * two modes.
 */
#define PROGIF_FLAG_CHANNEL0_MODE_NATIVE        (1U << 0)
#define PROGIF_FLAG_CHANNEL0_MODE_SWITCHABLE    (1U << 1)
#define PROGIF_FLAG_CHANNEL1_MODE_NATIVE        (1U << 2)
#define PROGIF_FLAG_CHANNEL1_MODE_SWITCHABLE    (1U << 3)
#define PROGIF_FLAG_BUSMASTER_SUPPORTED         (1U << 7)

static void reprogram_progif(pcipath path, uint8_t progif) {
    uint8_t new_progif = progif;
    if (
        !(progif & PROGIF_FLAG_CHANNEL0_MODE_NATIVE) &&
        (progif & PROGIF_FLAG_CHANNEL0_MODE_SWITCHABLE))
    {
        new_progif |= PROGIF_FLAG_CHANNEL0_MODE_NATIVE;
    }
    if (
        !(progif & PROGIF_FLAG_CHANNEL1_MODE_NATIVE) &&
        (progif & PROGIF_FLAG_CHANNEL1_MODE_SWITCHABLE))
    {
        new_progif |= PROGIF_FLAG_CHANNEL1_MODE_NATIVE;
    }
    // I don't even know if this is right way to reprogram Prog IF.
    if (progif != new_progif) {
        pci_printf(
            path, "idebus: reprogramming Prog IF value: %02x -> %02x\n",
            progif, new_progif);
        pci_writeprogif(path, new_progif);
        progif = pci_readprogif(path);
        if (progif != new_progif) {
            pci_printf(
                path, "idebus: failed to reprogram Prog IF - Using the value as-is\n");
        }
    }
}

static int read_chan_bar(
    uint16_t *io_base_out, uint16_t *ctrl_base_out, uint8_t *irq_out,
    pcipath path, int io_bar, int ctrl_bar, int chan)
{
    uintptr_t io_base = 0;
    uintptr_t ctrl_base = 0;
    uint8_t irq = 0;
    irq = pci_readinterruptline(path);
    int ret = pci_read_io_bar(&io_base, path, io_bar);
    if (ret < 0) {
        goto out;
    }
    ret = pci_read_io_bar(&ctrl_base, path, ctrl_bar);
    if (ret < 0) {
        goto out;
    }
    // Only the port at offset 2 is the real control port.
    ctrl_base += 2;
    ret = 0;
    pci_printf(
        path,
        "idebus: [channel%d] I/O base %#x, control base %#x, IRQ %d\n",
        chan, io_base, ctrl_base, irq);
out:
    if (ret < 0) {
        pci_printf(
            path, "idebus: could not read one of BARs for channel%d\n",
            chan);
    }
    *io_base_out = io_base;
    *ctrl_base_out = ctrl_base;
    *irq_out = irq;
    return ret;
}

static int read_busmaster_bar(uint16_t *base_out, pcipath path) {
    uintptr_t base = 0;
    int ret = pci_read_io_bar(&base, path, 4);
    if (ret < 0) {
        goto out;
    }
    ret = 0;
        pci_printf(path, "idebus: [busmaster] base %#x\n", base);
out:
    if (ret < 0) {
        pci_printf(
            path, "idebus: could not read the BAR for bus mastering");
    }
    *base_out = base;
    return ret;
}

static void pci_probe_callback(
    pcipath path, uint16_t venid, uint16_t devid, uint8_t baseclass,
    uint8_t subclass, void *data)
{
    (void)venid;
    (void)devid;
    (void)data;
    if ((baseclass != 0x1) || (subclass != 0x1)) {
        return;
    }
    uint8_t progif = pci_readprogif(path);
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

    uint16_t pcicmd = pci_readcmdreg(path);
    pcicmd |= PCI_CMDFLAG_IO_SPACE | PCI_CMDFLAG_MEMORY_SPACE | PCI_CMDFLAG_BUS_MASTER;
    pci_writecmdreg(path, pcicmd);

    if (CONFIG_REPROGRAM_PROGIF) {
        reprogram_progif(path, progif);
    }
    if (progif & PROGIF_FLAG_CHANNEL0_MODE_NATIVE) {
        int ret = read_chan_bar(
            &channel0_io_base, &channel0_ctrl_base,
            &channel0_irq, path, 0, 1, 0);
        if (ret < 0) {
            channel0_enabled = false;
        }
    }
    if (progif & PROGIF_FLAG_CHANNEL1_MODE_NATIVE) {
        int ret = read_chan_bar(
            &channel1_io_base, &channel1_ctrl_base,
            &channel1_irq, path, 2, 3, 1);
        if (ret < 0) {
            channel1_enabled = false;
        }
    }
    if (progif & PROGIF_FLAG_BUSMASTER_SUPPORTED) {
        int ret = read_busmaster_bar(
            &busmaster_io_base, path);
        if (ret < 0) {
            busmaster_enabled = false;
        }
    }
    struct shared *shared = heap_alloc(
        sizeof(*shared), HEAP_FLAG_ZEROMEMORY);
    if (shared == NULL) {
        pci_printf(path, "idebus: not enough memory\n");
        return;
    }
    shared->dma_lock_flag = false;
    /*
     * If Simplex Only is set, we need DMA lock to prevent both channels using 
     * DMA at the same time.
     */
    uint8_t bm_status = archi586_in8(
        busmaster_io_base + BUSMASTERREG_STATUS);
    shared->dma_lock_needed = bm_status & (1U << 7);
    if (channel0_enabled) {
        int ret = init_controller(
            shared, channel0_io_base, channel0_ctrl_base,
            busmaster_io_base, path, busmaster_enabled,
            channel0_irq, 0);
        if (ret < 0) {
            pci_printf(
                path,
                "idebus: [channel0] failed to initialize (error %d)\n",
                ret);
        }
    }
    if (channel1_enabled) {
         int ret = init_controller(
            shared, channel1_io_base, channel1_ctrl_base,
            busmaster_io_base + 8, path,
            busmaster_enabled, channel1_irq, 1);
        if (ret < 0) {
            pci_printf(
                path,
                "idebus: [channel1] failed to initialize (error %d)\n",
                ret);
        }
    }
}

void archi586_idebus_init(void) {
    pci_probe_bus(pci_probe_callback, NULL);
}
