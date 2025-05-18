#include <assert.h>
#include <errno.h>
#include <kernel/arch/pci.h>
#include <kernel/dev/pci.h>
#include <kernel/io/co.h>
#include <kernel/lib/diagnostics.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

/*
 * PCIPATH value structure:
 * xxxxxxxxyyyyyzzz
 * |       |    |
 * |       |    +-- Function(3-bit)
 * |       +------- Device (5-bit)
 * +--------------- Bus(8-bit)
 */

#define HEADERTYPE_GENERALDEVICE 0x0
#define HEADERTYPE_PCITOPCI 0x1
#define HEADERTYPE_PCITOCARDBUS 0x2

static uint8_t headertype(PCIPATH path) {
    return Pci_ReadConfigHeaderType(path) & 0x7f;
}

PCIPATH Pci_MakePath(uint8_t bus, uint8_t device, uint8_t function) {
    assert(device < 32);
    assert(function < 8);
    return (((uint32_t)bus) << 8) |
           ((uint32_t)device << 3) |
           (uint32_t)function;
}

uint8_t PciPath_GetBus(PCIPATH path) {
    return path >> 8;
}

uint8_t PciPath_GetDevice(PCIPATH path) {
    return ((uint32_t)path >> 3) & 0x1fU;
}

uint8_t PciPath_GetFunc(PCIPATH path) {
    return path & 0x7;
}

struct probecontext {
    void (*callback)(PCIPATH path, uint16_t venid, uint16_t devid, uint8_t baseclass, uint8_t subclass, void *data);
    void *data;
};

static void probe_bus(struct probecontext *self, uint8_t bus);

static void probe_dev(struct probecontext *self, uint8_t bus, uint8_t device) {
    uint16_t venid;
    uint16_t devid;
    Pci_ReadVenDevId(&venid, &devid, Pci_MakePath(bus, device, 0));
    if (venid == 0xffff) {
        return;
    }
    bool is_multifunc_dev = Pci_ReadConfigHeaderType(Pci_MakePath(bus, device, 0)) & 0x80;
    int maxfunccount = 1;
    if (is_multifunc_dev) {
        maxfunccount = 8;
    }
    for (int func = 0; func < maxfunccount; func++) {
        PCIPATH path = Pci_MakePath(bus, device, func);
        uint16_t venid;
        uint16_t devid;
        Pci_ReadVenDevId(&venid, &devid, path);
        if (venid == 0xffff) {
            continue;
        }

        uint8_t baseclass;
        uint8_t subclass;
        Pci_ReadClass(&baseclass, &subclass, path);
        if ((baseclass == 0x6) && (subclass == 0x4)) {
            if (headertype(path) != HEADERTYPE_PCITOPCI) {
                Pci_Printf(path, "WARNING: PCI-PCI bridge, but wrong config header type?\n");
            }
            uint32_t word = Arch_Pci_ReadConfig(path, 0x18);
            uint8_t primarybus = word & 0xffU;
            uint8_t secondarybus = (word >> 8) & 0xffU;
            if (primarybus != bus) {
                Pci_Printf(path, "WARNING: primary bus %u doesn't match this bus\n", primarybus);
            }
            probe_bus(self, secondarybus);
        }
        self->callback(path, venid, devid, baseclass, subclass, self->data);
    }
}

static void probe_bus(struct probecontext *self, uint8_t bus) {
    for (uint8_t device = 0; device < 32; device++) {
        probe_dev(self, bus, device);
    }
}

void Pci_ProbeBus(
    void (*callback)(PCIPATH path, uint16_t venid, uint16_t devid, uint8_t baseclass, uint8_t subclass, void *data),
    void *data) {
    struct probecontext ctx;
    ctx.callback = callback;
    ctx.data = data;
    bool is_multifunc_dev = Pci_ReadConfigHeaderType(Pci_MakePath(0, 0, 0)) & 0x80;
    int max_bus_count = 1;
    if (is_multifunc_dev) {
        max_bus_count = 8;
    }
    for (int bus = 0; bus < max_bus_count; bus++) {
        uint16_t venid;
        uint16_t devid;
        Pci_ReadVenDevId(&venid, &devid, Pci_MakePath(0, 0, bus));
        if (venid == 0xffff) {
            continue;
        }
        uint8_t baseclass;
        uint8_t subclass;
        Pci_ReadClass(&baseclass, &subclass, Pci_MakePath(0, 0, bus));
        if ((baseclass != 0x60) && (subclass != 0x00)) {
            /* Not a host bridge */
            continue;
        }
        probe_bus(&ctx, bus);
    }
}

void Pci_Printf(PCIPATH path, char const *fmt, ...) {
    Co_Printf("pci(%d,%d,%d): ", PciPath_GetBus(path), PciPath_GetDevice(path), PciPath_GetFunc(path));
    va_list ap;
    va_start(ap, fmt);
    Co_VPrintf(fmt, ap);
    va_end(ap);
}

void Pci_ReadVenDevId(uint16_t *venid_out, uint16_t *devid_out, PCIPATH path) {
    uint32_t word = Arch_Pci_ReadConfig(path, 0x00);
    *venid_out = word;
    *devid_out = word >> 16;
}

void Pci_ReadClass(uint8_t *baseclass_out, uint8_t *subclass_out, PCIPATH path) {
    uint32_t word = Arch_Pci_ReadConfig(path, 0x08);
    *baseclass_out = (word >> 24) & 0xffU;
    *subclass_out = (word >> 16) & 0xffU;
}

uint8_t Pci_ReadConfigHeaderType(PCIPATH path) {
    uint32_t word = Arch_Pci_ReadConfig(path, 0x0c);
    return (word >> 16) & 0xffU;
}

uint8_t Pci_ReadProgIF(PCIPATH path) {
    uint32_t word = Arch_Pci_ReadConfig(path, 0x08);
    return (word >> 8) & 0xffU;
}

void Pci_WriteProgIF(PCIPATH path, uint8_t progif) {
    uint32_t word = Arch_Pci_ReadConfig(path, 0x08);
    Arch_Pci_WriteConfig(path, 0x08, (word & ~(0xffU << 8)) | ((uint32_t)progif << 8));
}

uint8_t Pci_ReadIrqLine(PCIPATH path) {
    uint32_t word = Arch_Pci_ReadConfig(path, 0x3c);
    return word & 0xffU;
}

uint16_t Pci_ReadCmdReg(PCIPATH path) {
    uint32_t word = Arch_Pci_ReadConfig(path, 0x4);
    return word & 0xffffU;
}

void Pci_WriteCmdReg(PCIPATH path, uint16_t value) {
    /* Status register part of the word is either R/O or RWC(read/write-to-clear), so we can just leave those zero and shouldn't affect it. */
    Arch_Pci_WriteConfig(path, 0x4, value);
}

uint16_t Pci_ReadStatusReg(PCIPATH path) {
    uint32_t word = Arch_Pci_ReadConfig(path, 0x4);
    return (word >> 16) & 0xffffU;
}

void Pci_WriteStatusReg(PCIPATH path, uint16_t value) {
    uint32_t word = Arch_Pci_ReadConfig(path, 0x4);
    /* Make sure we preserve the command register. */
    Arch_Pci_WriteConfig(path, 0x4, ((uint32_t)value << 16) | (word & 0xffffU));
}

/* TODO: Memory BAR support is currently incomplete, because it doesn't query how much address space is used. */
[[nodiscard]] int Pci_ReadBar(uintptr_t *addr_out, bool *isiobar_out, bool *isprefetchable_out, PCIPATH path, uint8_t bar) {
    assert(bar <= 5);
    uint8_t regoffset = 0x10 + (bar * 4);
    uint32_t word = Arch_Pci_ReadConfig(path, regoffset);
    if ((word & 0x1U) == 0) {
        /* Memory space BAR */
        uint8_t type = (word >> 1) & 0x3U;
        uintptr_t base_addr;
        switch (type) {
        case 0x0:
        case 0x1:
            /* 32-bit and 16-bit memory space BAR */
            base_addr = word & ~0xfU;
            break;
        case 0x2:
            Pci_Printf(path, "BAR%d: 64-bit BAR is not supported", bar);
            return -EIO;
        default:
            Pci_Printf(path, "BAR%d: unrecognized BAR type %d", bar, type);
            return -EIO;
        }
        *addr_out = base_addr;
        *isiobar_out = false;
        *isprefetchable_out = word & (1U << 3);
    } else {
        /* I/O space BAR */
        uint32_t baseaddr = word & ~0x3U;
        *addr_out = baseaddr;
        *isiobar_out = true;
        *isprefetchable_out = false;
    }
    return 0;
}

[[nodiscard]] int Pci_ReadMemBar(uintptr_t *addr_out, bool *isprefetchable_out, PCIPATH path, int bar) {
    bool isiobar;
    int ret = Pci_ReadBar(addr_out, &isiobar, isprefetchable_out, path, bar);
    if (ret < 0) {
        return ret;
    }
    if (isiobar) {
        Pci_Printf(path, "BAR%d: expected memory BAR, got I/O BAR", path, bar);
        return -EIO;
    }
    return 0;
}

[[nodiscard]] int Pci_ReadIoBar(uintptr_t *addr_out, PCIPATH path, int bar) {
    bool is_iobar, is_prefetchable_out;
    int ret = Pci_ReadBar(addr_out, &is_iobar, &is_prefetchable_out, path, bar);
    if (ret < 0) {
        return ret;
    }
    if (!is_iobar) {
        Pci_Printf(path, "BAR%d: expected I/O BAR, got memory BAR", bar);
        return -EIO;
    }
    return 0;
}

static void print_callback(PCIPATH path, uint16_t venid, uint16_t devid, uint8_t baseclass, uint8_t subclass, void *data) {
    (void)data;
    Pci_Printf(path, "%04x:%04x class %02x:%02x\n", venid, devid, baseclass, subclass);
}

void Pci_PrintBus(void) {
    Pci_ProbeBus(print_callback, NULL);
}
