#include "../ioport.h"
#include "../pic.h"
#include "idebus.h"
#include <assert.h>
#include <kernel/arch/interrupts.h>
#include <kernel/arch/iodelay.h>
#include <kernel/arch/mmu.h>
#include <kernel/dev/atadisk.h>
#include <kernel/dev/pci.h>
#include <kernel/io/tty.h>
#include <kernel/lib/miscmath.h>
#include <kernel/lib/pstring.h>
#include <kernel/mem/heap.h>
#include <kernel/mem/pmm.h>
#include <kernel/mem/vmm.h>
#include <kernel/status.h>
#include <kernel/trapmanager.h>
#include <kernel/types.h>
#include <stdalign.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static uint16_t const PRD_FLAG_LAST_ENTRY_IN_PRDT = 1 << 15;

// Physical Region Descriptor
struct prd {
    uint32_t bufferphysbase;
    uint16_t len;   // 0 = 64K
    uint16_t flags; // All bits are reserved(should be 0) except for top bit(PRD_FLAG_LAST_ENTRY_IN_PRDT)
};

// shared_t data between two IDE buses in a controller
struct shared {
    struct bus *buses[2];
    _Atomic bool dmalockflag;
    bool dmalockneeded : 1;
};

struct bus {
    physptr prdtphysbase;
    size_t prdcount;
    struct prd *prdt;
    struct shared *shared;
    void *dmabuffer;
    struct archx86_pic_irqhandler irqhandler;
    uint16_t iobase;
    uint16_t ctrlbase;
    uint16_t busmastrerbase;
    pcipath pcipath;
    int8_t lastselecteddrive; // -1: No device was selected before
    _Atomic bool buslockflag;
    _Atomic bool gotirq;
    bool isdmaread        : 1;
    bool busmasterenabled : 1;
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

static uint8_t const DRIVE_AND_HEAD_FLAG_DRV = 1 << 4;
static uint8_t const DRIVE_AND_HEAD_FLAG_LBA = 1 << 6;

static uint8_t const DRVICE_CONTROL_FLAG_NIEN = 1 << 1;
static uint8_t const DRVICE_CONTROL_FLAG_SRST = 1 << 2;
static uint8_t const DRVICE_CONTROL_FLAG_HOB  = 1 << 7;

static void ioout8(struct bus *self, enum ioreg reg, uint8_t data) {
    archx86_out8(self->iobase + reg, data);
}
static void ioout16(struct bus *self, enum ioreg reg, uint16_t data) {
    archx86_out16(self->iobase + reg, data);
}
static uint8_t ioin8(struct bus *self, enum ioreg reg) {
    return archx86_in8(self->iobase + reg);
}
static uint16_t ioin16(struct bus *self, enum ioreg reg) {
    return archx86_in16(self->iobase + reg);
}
static void ioin16rep(struct bus *self, enum ioreg reg, void *buf, size_t len) {
    archx86_in16rep(self->iobase + reg, buf, len);
}

enum ctrlreg {
    CTRLREG_ALTERNATESTATUS = 0, // read
    CTRLREG_DEVICECONTROL   = 0, // write
};

static void ctrlout8(struct bus *self, enum ctrlreg reg, uint8_t data) {
    archx86_out8(self->ctrlbase + reg, data);
}
static void ctrlout16(struct bus *self, enum ioreg reg, uint16_t data) {
    archx86_out16(self->ctrlbase + reg, data);
}
static uint8_t ctrlin8(struct bus *self, enum ctrlreg reg) {
    return archx86_in8(self->ctrlbase + reg);
}
static uint16_t ctrlin16(struct bus *self, enum ctrlreg reg) {
    return archx86_in16(self->ctrlbase + reg);
}

static uint8_t readstatus(struct bus *self) {
    return ctrlin8(self, CTRLREG_ALTERNATESTATUS);
}

static void busprintf(struct bus *self, char const *fmt, ...) {
    va_list ap;

    tty_printf("idebus(%x): ", self->iobase);
    va_start(ap, fmt);
    tty_vprintf(fmt, ap);
    va_end(ap);
}

static void driveprintf(struct bus *self, int drive, char const *fmt, ...) {
    va_list ap;

    tty_printf("idebus(%x)drive(%d): ", self->iobase, drive);
    va_start(ap, fmt);
    tty_vprintf(fmt, ap);
    va_end(ap);
}

static void resetbus(struct bus *self) {
    uint8_t regvalue = ctrlin8(self, CTRLREG_DEVICECONTROL);
    regvalue &= ~(DRVICE_CONTROL_FLAG_NIEN | DRVICE_CONTROL_FLAG_HOB);
    regvalue |= DRVICE_CONTROL_FLAG_SRST;
    ctrlout8(self, CTRLREG_DEVICECONTROL, regvalue);
    arch_iodelay();
    regvalue &= ~DRVICE_CONTROL_FLAG_SRST;
    ctrlout8(self, CTRLREG_DEVICECONTROL, regvalue);
}

static FAILABLE_FUNCTION selectdrive(struct bus *self, int8_t drive) {
FAILABLE_PROLOGUE
    assert((0 == drive) || (1 == drive));
    uint8_t regvalue = ioin8(self, IOREG_DRIVE_AND_HEAD);
    if (drive == 0) {
        regvalue &= ~DRIVE_AND_HEAD_FLAG_DRV;
    } else {
        regvalue |= DRIVE_AND_HEAD_FLAG_DRV;
    }
    regvalue |= DRIVE_AND_HEAD_FLAG_LBA;
    ioout8(self, IOREG_DRIVE_AND_HEAD, regvalue);
    if (self->lastselecteddrive != drive) {
        for (size_t i = 0; i < 14; i++) {
            readstatus(self);
        }
        self->lastselecteddrive = drive;
    }
FAILABLE_EPILOGUE_BEGIN
FAILABLE_EPILOGUE_END
}

static void atadisk_op_softreset(struct atadisk *self) {
    struct disk *disk = self->data;
    resetbus(disk->bus);
}
static FAILABLE_FUNCTION atadisk_op_selectdisk(struct atadisk *self) {
    struct disk *disk = self->data;
    return selectdrive(disk->bus, disk->driveid);
}
static uint8_t atadisk_op_readstatus(struct atadisk *self) {
    struct disk *disk = self->data;
    return readstatus(disk->bus);
}
static void atadisk_op_setfeaturesparam(struct atadisk *self, uint16_t data) {
    struct disk *disk = self->data;
    ioout8(disk->bus, IOREG_FEATURES, data);
}
static void atadisk_op_setcountparam(struct atadisk *self, uint16_t data) {
    struct disk *disk = self->data;
    ioout8(disk->bus, IOREG_SECTORCOUNT, data);
}
static void atadisk_op_setlbaparam(struct atadisk *self, uint32_t data) {
    struct disk *disk = self->data;
    ioout8(disk->bus, IOREG_LBA_LO, data);
    ioout8(disk->bus, IOREG_LBA_MID, data >> 8);
    ioout8(disk->bus, IOREG_LBA_HI, data >> 16);
    uint8_t regvalue = ioin8(disk->bus, IOREG_DRIVE_AND_HEAD);
    regvalue = (regvalue & ~0x0F) | ((data >> 24) & 0x0F);
    ioout8(disk->bus, IOREG_DRIVE_AND_HEAD, regvalue);
}
static void atadisk_op_setdeviceparam(struct atadisk *self, uint8_t data) {
    struct disk *disk = self->data;
    uint8_t regvalue = ioin8(disk->bus, IOREG_DRIVE_AND_HEAD);
    // Note that we preserve lower 3-bit, which contains upper 4-bit of LBA.
    // (ACS-3 calls these bits as "reserved", and maybe this is the reason?)
    regvalue = (data & ~0x0F) | (regvalue & 0x0F);
    ioout8(disk->bus, IOREG_DRIVE_AND_HEAD, regvalue);
}
static uint32_t atadisk_op_getlbaoutput(struct atadisk *self) {
    struct disk *disk = self->data;
    uint32_t lbalo  = ioin8(disk->bus, IOREG_LBA_LO);
    uint32_t lbamid = ioin8(disk->bus, IOREG_LBA_MID);
    uint32_t lbahi  = ioin8(disk->bus, IOREG_LBA_HI);
    uint32_t lbatop4bit = ioin8(disk->bus, IOREG_DRIVE_AND_HEAD) & 0x0F;
    return (lbatop4bit << 24) | (lbahi << 16) | (lbamid << 8) | lbalo;
}

static void atadisk_op_issuecmd(struct atadisk *self, enum ata_cmd cmd) {
    struct disk *disk = self->data;
    ioout8(disk->bus, IOREG_COMMAND, cmd);
}
static bool atadisk_op_getirqflag(struct atadisk *self) {
    struct disk *disk = self->data;
    return disk->bus->gotirq;
}
static void atadisk_op_clearirqflag(struct atadisk *self) {
    struct disk *disk = self->data;
    disk->bus->gotirq = false;
}
static void atadisk_op_readdata(struct ata_databuf *out, struct atadisk *self) {
    struct disk *disk = self->data;
    ioin16rep(disk->bus, IOREG_DATA, out->data, sizeof(out->data)/sizeof(*out->data));
}
static void atadisk_op_writedata(struct atadisk *self, struct ata_databuf *buffer) {
    struct disk *disk = self->data;
    for (size_t i = 0; i < 256; i++) {
        ioout16(disk->bus, IOREG_DATA, buffer->data[i]);
        arch_iodelay();
    }
}

enum busmasterreg {
    BUSMASTERREG_CMD  = 0,
    BUSMASTERREG_STATUS   = 2,
    BUSMASTERREG_PRDTADDR = 4,
}; 

static void busmasterout8(struct bus *self, enum busmasterreg reg, uint8_t data) {
    assert(self->busmasterenabled);
    archx86_out8(self->busmastrerbase + reg, data);
}
static void busmasterout32(struct bus *self, enum busmasterreg reg, uint32_t data) {
    assert(self->busmasterenabled);
    archx86_out32(self->busmastrerbase + reg, data);
}
static uint8_t busmasterin8(struct bus *self, enum busmasterreg reg) {
    assert(self->busmasterenabled);
    return archx86_in8(self->busmastrerbase + reg);
}
static uint32_t busmasterin32(struct bus *self, enum busmasterreg reg) {
    assert(self->busmasterenabled);
    return archx86_in32(self->busmastrerbase + reg);
}

static uint8_t BUSMASTER_CMDFLAG_START = 1 << 0;
static uint8_t BUSMASTER_CMDFLAG_READ  = 1 << 3;

enum {
    MAX_TRANSFTER_SIZE_PER_PRD   = 65536,
    MAX_DMA_TRANSFER_SIZE_NEEDED = ATA_MAX_SECTORS_PER_TRANSFER * ATA_SECTOR_SIZE
};

static bool atadisk_op_dma_beginsession(struct atadisk *self) {
    struct disk *disk = self->data;
    struct bus *bus = disk->bus;
    if (!bus->busmasterenabled) {
        // No DMA support
        return false;
    }
    if (!bus->shared->dmalockneeded) {
        // No DMA lock is used - We are good to go.
        return true;
    }
    // If DMA lock is present, that means both IDE channels can't use DMA at the same time.
    // So we must lock the DMA, and if we can't, we have to fall back to PIO.
    bool expected = false;
    bool desired = true;
    if (!atomic_compare_exchange_strong_explicit(&bus->shared->dmalockflag, &expected, desired, memory_order_acquire, memory_order_relaxed)) {
        // Someone else is using DMA
        return false;
    }
    return true;
}

static void atadisk_op_dma_endsession(struct atadisk *self) {
    struct disk *disk = self->data;
    struct bus *bus = disk->bus;
    assert(bus->busmasterenabled);
    if (!bus->shared->dmalockneeded) {
        return;
    }
    bool desired = false;
    atomic_store_explicit(&bus->shared->dmalockflag, desired, memory_order_release);
}

static void atadisk_op_lock(struct atadisk *self) {
    struct disk *disk = self->data;
    struct bus *bus = disk->bus;
    bool expected = false;
    bool desired = true;
    while (!atomic_compare_exchange_strong_explicit(&bus->buslockflag, &expected, desired, memory_order_acquire, memory_order_relaxed)) {}
}

static void atadisk_op_unlock(struct atadisk *self) {
    struct disk *disk = self->data;
    struct bus *bus = disk->bus;
    bool desired = false;
    atomic_store_explicit(&bus->buslockflag, desired, memory_order_release);
}

static FAILABLE_FUNCTION atadisk_op_dma_inittransfter(struct atadisk *self, void *buffer, size_t len, bool isread) {
FAILABLE_PROLOGUE
    struct disk *disk = self->data;
    struct bus *bus = disk->bus;
    assert(bus->busmasterenabled);
    assert(len <= MAX_DMA_TRANSFER_SIZE_NEEDED);
    bus->dmabuffer = buffer;
    bus->isdmaread = isread;
    size_t remainingsize = len;
    for (size_t i = 0; remainingsize != 0; i++) {
        assert(i < bus->prdcount);
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
            pmemcpy_out(bus->prdt[i].bufferphysbase, &((uint8_t *)buffer)[i * MAX_TRANSFTER_SIZE_PER_PRD], currentsize, true);
        }
        remainingsize -= currentsize;
    }
    // Setup busmaster registers
    busmasterout32(bus, BUSMASTERREG_PRDTADDR, bus->prdtphysbase); // PRDT address
    uint8_t cmdvalue = 0;
    if (isread) {
        cmdvalue |= BUSMASTER_CMDFLAG_READ;
    } else {
        cmdvalue &= ~BUSMASTER_CMDFLAG_READ;
    }
    busmasterout8(bus, BUSMASTERREG_CMD, cmdvalue);
    busmasterout8(bus, BUSMASTERREG_STATUS, 0x06);

FAILABLE_EPILOGUE_BEGIN
FAILABLE_EPILOGUE_END
}

static FAILABLE_FUNCTION atadisk_op_dma_begintransfer(struct atadisk *self) {
FAILABLE_PROLOGUE
    struct disk *disk = self->data;
    struct bus *bus = disk->bus;
    busmasterout8(bus, BUSMASTERREG_CMD, busmasterin8(bus, BUSMASTERREG_CMD) | BUSMASTER_CMDFLAG_START);
FAILABLE_EPILOGUE_BEGIN
FAILABLE_EPILOGUE_END
}

static enum ata_dmastatus atadisk_op_dma_checktransfer(struct atadisk *self) {
    struct disk *disk = self->data;
    struct bus *bus = disk->bus;
    uint8_t bmstatus = busmasterin8(bus, BUSMASTERREG_STATUS); // We have to read the status after IRQ.
    if (bmstatus & (1 << 1)) {
        uint16_t pcistatus = pci_readstatusreg(bus->pcipath);
        driveprintf(bus, disk->driveid, "DMA error occured. busmaster status %02x, PCI status %04x\n", bmstatus, pcistatus);
        pci_writestatusreg(bus->pcipath, PCI_STATUSFLAG_MASTER_DATA_PARITY_ERROR | PCI_STATUSFLAG_RECEIVED_TARGET_ABORT | PCI_STATUSFLAG_RECEIVED_MASTER_ABORT);
        if (pcistatus & PCI_STATUSFLAG_MASTER_DATA_PARITY_ERROR) {
            return ATA_DMASTATUS_FAIL_UDMA_CRC;
        } else {
            return ATA_DMASTATUS_FAIL_OTHER_IO;
        }
    }
    if (!(bmstatus & (1 << 0))) {
        pci_writestatusreg(bus->pcipath, PCI_STATUSFLAG_MASTER_DATA_PARITY_ERROR | PCI_STATUSFLAG_RECEIVED_TARGET_ABORT | PCI_STATUSFLAG_RECEIVED_MASTER_ABORT);
        return ATA_DMASTATUS_SUCCESS;
    }

    return ATA_DMASTATUS_BUSY;
}

static void atadisk_op_dma_endtransfer(struct atadisk *self, bool wassuccess) {
    struct disk *disk = self->data;
    struct bus *bus = disk->bus;
    busmasterout8(bus, BUSMASTERREG_CMD, busmasterin8(bus, BUSMASTERREG_CMD) & ~BUSMASTER_CMDFLAG_START);
    if (bus->isdmaread && wassuccess) {
        for (size_t i = 0; i < bus->prdcount; i++) {
            size_t size = bus->prdt[i].len;
            if (size == 0) {
                size = 65536;
            }
            pmemcpy_in(&((uint8_t *)bus->dmabuffer)[i * MAX_TRANSFTER_SIZE_PER_PRD], bus->prdt[i].bufferphysbase, size, true);
            if (bus->prdt[i].flags & PRD_FLAG_LAST_ENTRY_IN_PRDT) {
                break;
            }
        }
    }
}

static void atadisk_op_dma_deinittransfer(struct atadisk *self) {
    struct disk *disk = self->data;
    struct bus *bus = disk->bus;
    assert(bus->busmasterenabled);
}

static struct atadisk_ops const OPS = {
    .dma_beginsession   = atadisk_op_dma_beginsession,
    .dma_endsession     = atadisk_op_dma_endsession,
    .lock               = atadisk_op_lock,
    .unlock             = atadisk_op_unlock,
    .readstatus         = atadisk_op_readstatus,
    .selectdisk         = atadisk_op_selectdisk,
    .setfeaturesparam   = atadisk_op_setfeaturesparam,
    .setcountparam      = atadisk_op_setcountparam,
    .setlbaparam        = atadisk_op_setlbaparam,
    .setdeviceparam     = atadisk_op_setdeviceparam,
    .getlbaoutput       = atadisk_op_getlbaoutput,
    .issuecmd           = atadisk_op_issuecmd,
    .getirqflag         = atadisk_op_getirqflag,
    .clearirqflag       = atadisk_op_clearirqflag,
    .readdata           = atadisk_op_readdata,
    .writedata          = atadisk_op_writedata,
    .dma_inittransfer   = atadisk_op_dma_inittransfter,
    .dma_begintransfer  = atadisk_op_dma_begintransfer,
    .dma_checktransfer  = atadisk_op_dma_checktransfer,
    .dma_endtransfer    = atadisk_op_dma_endtransfer,
    .dma_deinittransfer = atadisk_op_dma_deinittransfer,
    .softreset          = atadisk_op_softreset,
};

static void irqhandler(int irqnum,  void *data) {
    struct bus *bus = data;
    bus->gotirq = true;
    ioin8(bus, IOREG_STATUS);
    archx86_pic_sendeoi(irqnum);
}

static FAILABLE_FUNCTION initcontroller(struct shared *shared, uint16_t iobase, uint16_t ctrlbase, uint16_t busmastrerbase, pcipath pcipath, bool busmasterenabled, uint8_t irq, size_t channalindex) {
FAILABLE_PROLOGUE
    struct bus *bus = heap_alloc(sizeof(*bus), HEAP_FLAG_ZEROMEMORY);
    if (bus == NULL) {
        THROW(ERR_NOMEM);
    }
    bus->shared = shared;
    bus->pcipath = pcipath;
    bus->iobase = iobase;
    bus->ctrlbase = ctrlbase;
    bus->busmastrerbase = busmastrerbase;
    bus->lastselecteddrive = -1;

    if (busmasterenabled) {
        // This should be enough to store allocated page counts.
        size_t pagecounts[(MAX_DMA_TRANSFER_SIZE_NEEDED / MAX_TRANSFTER_SIZE_PER_PRD) + 1];
        // Allocate resources needed for busmastering DMA
        bus->prdcount = sizetoblocks(MAX_DMA_TRANSFER_SIZE_NEEDED, MAX_TRANSFTER_SIZE_PER_PRD);
        size_t prdtsize = bus->prdcount * sizeof(*bus->prdt);
        assert(prdtsize < MAX_TRANSFTER_SIZE_PER_PRD);
        size_t prdtpagecount = sizetoblocks(prdtsize, ARCH_PAGESIZE);
        bool physallocok = false;
        struct vmobject *prdtvmobject = NULL;
        size_t allocatedprdtcount = 0;
        status_t status = pmm_alloc(&bus->prdtphysbase, &prdtpagecount);
        if (status != OK) {
            busprintf(bus, "failed to allocate pages for busmaster PRDT(error %d)\n", status);
            goto nobusmaster;
        }
        physallocok = true;
        status = vmm_map(&prdtvmobject, vmm_get_kernel_addressspace(), bus->prdtphysbase, prdtpagecount * ARCH_PAGESIZE, MAP_PROT_READ | MAP_PROT_WRITE | MAP_PROT_NOCACHE);
        if (status != OK) {
            busprintf(bus, "failed to map pages for busmaster PRDT(error %d)\n", status);
            goto nobusmaster;
        }
        bus->prdt = (void *)prdtvmobject->startaddress;
        memset(bus->prdt, 0, prdtsize);
        size_t remainingsize = MAX_DMA_TRANSFER_SIZE_NEEDED;
        for (size_t i = 0; i < bus->prdcount; i++) {
            size_t currentsize = remainingsize;
            if (MAX_TRANSFTER_SIZE_PER_PRD < currentsize) {
                currentsize = MAX_TRANSFTER_SIZE_PER_PRD;
            }
            // NOTE: We setup PRD's len and flags when we initialize DMA transfer.
            size_t currentpagecount = sizetoblocks(currentsize, ARCH_PAGESIZE);
            TRY(pmm_alloc(&bus->prdt[i].bufferphysbase, &currentpagecount));
            pagecounts[i] = currentpagecount;
            pmemset(bus->prdt[i].bufferphysbase, 0x00, currentsize, true);
            allocatedprdtcount++;
            remainingsize -= currentsize;
        }
        goto cont;
    nobusmaster:
        busprintf(bus, "an error occured while initializing busmaster DMA. falling back to PIO-only.\n");
        for (size_t i = 0; i < allocatedprdtcount; i++) {
            pmm_free(bus->prdt[i].bufferphysbase, pagecounts[i]);
        }
        if (prdtvmobject != NULL) {
            vmm_free(prdtvmobject);
        }
        if (physallocok) {
            pmm_free(bus->prdtphysbase, prdtpagecount);
        }
        busmasterenabled = false;
    }
cont:
    bus->busmasterenabled = busmasterenabled;

    uint8_t busstatus = readstatus(bus);
    if ((busstatus & 0x7f) == 0x7f) {
        busprintf(bus, "seems to be floating(got status byte %#x)\n", busstatus);
        THROW(ERR_IO);
    }
    // Prepare to receive IRQs
    archx86_pic_registerhandler(&bus->irqhandler, irq, irqhandler, bus);
    shared->buses[channalindex] = bus;
    archx86_pic_unmaskirq(irq);
    resetbus(bus);
    // Some systems seem to fire IRQ after reset.
    for (uint8_t i = 0; i < 255; i++) {
        if (bus->gotirq) {
            bus->gotirq = false;
            break;
        }
        arch_iodelay();
    }
    busprintf(bus, "probing the bus\n");
    for (int8_t drive = 0; drive < 2; drive++) {
        status_t status = selectdrive(bus, drive);
        if (status != OK) {
            driveprintf(bus, drive, "cannot select\n");
            continue;
        }
        struct disk *disk = heap_alloc(sizeof(*disk), HEAP_FLAG_ZEROMEMORY);
        if (disk == NULL) {
            continue;
        }
        disk->bus = bus;
        disk->driveid = drive;
        status = atadisk_register(&disk->atadisk, &OPS, disk);
        if (status != OK) {
            if (status == ERR_NODEV) {
                driveprintf(bus, drive, "nothing there or non-accessible\n");
            } else {
                driveprintf(bus, drive, "failed to initialize disk (error %d)\n", status);
            }
            heap_free(disk);
            continue;
        }
        driveprintf(bus, drive, "disk registered\n");
    }
    busprintf(bus, "bus probing complete\n");

FAILABLE_EPILOGUE_BEGIN
    if (DID_FAIL) {
        heap_free(bus);
    }
FAILABLE_EPILOGUE_END
}

static void pciprobecallback(pcipath path, uint16_t venid, uint16_t devid, uint8_t baseclass, uint8_t subclass, void *data) {
    enum {
        // Each channel can be either in native or compatibility mode(~_NATIVE flag set means it's in native mode),
        // and ~_SWITCHABLE flag means whether it is possible to switch between two modes.
        PROGIF_FLAG_CHANNEL0_MODE_NATIVE     = 1 << 0,
        PROGIF_FLAG_CHANNEL0_MODE_SWITCHABLE = 1 << 1,
        PROGIF_FLAG_CHANNEL1_MODE_NATIVE     = 1 << 2,
        PROGIF_FLAG_CHANNEL1_MODE_SWITCHABLE = 1 << 3,
        PROGIF_FLAG_BUSMASTER_SUPPORTED      = 1 << 7,
    };
    (void)venid;
    (void)devid;
    (void)data;
    if ((baseclass != 0x1) || (subclass != 0x1)) {
        return;
    }
    uint8_t progif = pci_readprogif(path);
    uint8_t channel0irq = 14;
    uint8_t channel1irq = 15;
    // These are uintptr_t instead of uint16_t, because PCI APIs want pointers to uintptr_t values.
    uintptr_t channel0iobase = 0x1F0;
    uintptr_t channel0ctrlbase = 0x3F6;
    uintptr_t channel1iobase = 0x170;
    uintptr_t channel1ctrlbase = 0x376;
    uintptr_t busmasteriobase = 0;
    bool channel0enabled = true;
    bool channel1enabled = true;
    bool busmasterenabled = true;
    status_t status;

    uint16_t pcicmd = pci_readcmdreg(path);
    pcicmd |= PCI_CMDFLAG_IO_SPACE | PCI_CMDFLAG_MEMORY_SPACE | PCI_CMDFLAG_BUS_MASTER;
    pci_writecmdreg(path, pcicmd);

#if 0
    uint8_t newprogif = progif;
    if (!(progif & PROGIF_FLAG_CHANNEL0_MODE_NATIVE) && (progif & PROGIF_FLAG_CHANNEL0_MODE_SWITCHABLE)) {
        newprogif |= PROGIF_FLAG_CHANNEL0_MODE_NATIVE;
    }
    if (!(progif & PROGIF_FLAG_CHANNEL1_MODE_NATIVE) && (progif & PROGIF_FLAG_CHANNEL1_MODE_SWITCHABLE)) {
        newprogif |= PROGIF_FLAG_CHANNEL1_MODE_NATIVE;
    }
    // I don't even know if this is right way to reprogram Prog IF.
    if (progif != newprogif) {
        pci_printf(path, "idebus: reprogramming Prog IF value: %02x -> %02x\n", progif, newprogif);
        pci_writeprogif(path, newprogif);
        progif = pci_readprogif(path);
        if (progif != newprogif) {
            pci_printf(path, "idebus: failed to reprogram Prog IF - Using the value as-is\n");
        }
    }
#endif
    if (progif & PROGIF_FLAG_CHANNEL0_MODE_NATIVE) {
        channel0irq = pci_readinterruptline(path);
        // Channel 0 is in native mode
        status = pci_readiobar(&channel0iobase, path, 0);
        if (status != OK) {
            goto channel0barfail;
        }
        status = pci_readiobar(&channel0ctrlbase, path, 1);
        if (status != OK) {
            goto channel0barfail;
        }
        // Only the port at offset 2 is the real control port.
        channel0ctrlbase += 2;
    }
    goto channel1;
channel0barfail:
    channel0enabled = false;
    pci_printf(path, "idebus: could not read one of BARs for channel 0\n");
channel1:
    if (progif & PROGIF_FLAG_CHANNEL1_MODE_NATIVE) {
        channel1irq = pci_readinterruptline(path);
        // Channel 1 is in native mode
        status = pci_readiobar(&channel1iobase, path, 2);
        if (status != OK) {
            goto channel1barfail;
        }
        status = pci_readiobar(&channel1ctrlbase, path, 3);
        if (status != OK) {
            goto channel1barfail;
        }
        // Only the port at offset 2 is the real control port.
        channel1ctrlbase += 2;
    }
    goto busmaster;
channel1barfail:
    channel1enabled = false;
    pci_printf(path, "idebus: could not read one of BARs for channel 1\n");
busmaster:
    if (progif & PROGIF_FLAG_BUSMASTER_SUPPORTED) {
        status = pci_readiobar(&busmasteriobase, path, 4);
        if (status != OK) {
            goto busmasterbarfail;
        }
    } else {
        busmasterenabled = false;
    }
    goto doinit;
busmasterbarfail:
    busmasterenabled = false;
    pci_printf(path, "idebus: could not read one of BARs for bus mastering\n");
doinit:
    if (channel0enabled) {
        pci_printf(path, "idebus: [channel0] I/O base %#x, control base %#x, IRQ %d\n", channel0iobase, channel0ctrlbase, channel0irq);
    }
    if (channel1enabled) {
        pci_printf(path, "idebus: [channel1] I/O base %#x, control base %#x, IRQ %d\n", channel1iobase, channel1ctrlbase, channel1irq);
    }
    if (busmasterenabled) {
        pci_printf(path, "idebus: [busmaster] base %#x\n", busmasteriobase);
    }
    struct shared *shared = heap_alloc(sizeof(*shared), HEAP_FLAG_ZEROMEMORY);
    if (shared == NULL) {
        pci_printf(path, "idebus: not enough memory\n");
        return;
    }
    shared->dmalockflag = false;
    // If Simplex Only is set, we need DMA lock to prevent both channels using DMA at the same time.
    shared->dmalockneeded = archx86_in8(busmasteriobase + BUSMASTERREG_STATUS) & (1 << 7);
    if (channel0enabled) {
        status = initcontroller(shared, channel0iobase, channel0ctrlbase, busmasteriobase, path, busmasterenabled, channel0irq, 0);
        if (status != OK) {
            pci_printf(path, "idebus: [channel0] failed to initialize (error %d)\n", status);
        }
    }
    if (channel1enabled) {
        status = initcontroller(shared, channel1iobase, channel1ctrlbase, busmasteriobase + 8, path, busmasterenabled, channel1irq, 1);
        if (status != OK) {
            pci_printf(path, "idebus: [channel1] failed to initialize (error %d)\n", status);
        }
    }
}

void archx86_idebus_init(void) {
    pci_probebus(pciprobecallback, NULL);
}
