#include <assert.h>
#include <errno.h>
#include <kernel/arch/pci.h>
#include <kernel/dev/pci.h>
#include <kernel/io/co.h>
#include <kernel/lib/diagnostics.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

// pcipath value structure:
// xxxxxxxxyyyyyzzz
// |       |    |
// |       |    +-- Function(3-bit)
// |       +------- Device (5-bit)
// +--------------- Bus(8-bit)

#define HEADERTYPE_GENERALDEVICE    0x0
#define HEADERTYPE_PCITOPCI         0x1
#define HEADERTYPE_PCITOCARDBUS     0x2

static uint8_t headertype(pcipath path) {
    return pci_read_config_header_type(path) & 0x7f;
}


pcipath pci_makepath(uint8_t bus, uint8_t device, uint8_t function) {
    assert(device < 32);
    assert(function < 8);
    return  (((uint32_t)bus) << 8) |
            ((uint32_t)device << 3) |
            (uint32_t)function;
}

uint8_t pcipath_getbus(pcipath path) {
    return path >> 8;
}

uint8_t pcipath_getdev(pcipath path) {
    return ((uint32_t)path >> 3) & 0x1fU;
}

uint8_t pcipath_getfunc(pcipath path) {
    return path & 0x7;
}

struct probecontext {
    void (*callback)(pcipath path, uint16_t venid, uint16_t devid, uint8_t baseclass, uint8_t subclass, void *data);
    void *data;
};

static void probe_bus(struct probecontext *self, uint8_t bus);

// NOLINTNEXTLINE(misc-no-recursion)
static void probe_dev(struct probecontext *self, uint8_t bus, uint8_t device) {
    uint16_t venid;
    uint16_t devid;
    pci_readvendevid(&venid, &devid, pci_makepath(bus, device, 0));
    if (venid == 0xffff) {
        return;
    }
    bool ismultifuncdev = pci_read_config_header_type(
        pci_makepath(bus, device, 0)) & 0x80;
    int maxfunccount = 1;
    if (ismultifuncdev) {
        maxfunccount = 8;
    }
    for (int func = 0; func < maxfunccount; func++) {
        pcipath path = pci_makepath(bus, device, func);
        uint16_t venid;
        uint16_t devid;
        pci_readvendevid(&venid, &devid, path);
        if (venid == 0xffff) {
            continue;
        }

        uint8_t baseclass;
        uint8_t subclass;
        pci_readclass(&baseclass, &subclass, path);
        if ((baseclass == 0x6) && (subclass == 0x4)) {
            if (headertype(path) != HEADERTYPE_PCITOPCI) {
                pci_printf(path, "WARNING: PCI-PCI bridge, but wrong config header type?\n");
            }
            uint32_t word = arch_pci_readconfig(path, 0x18);
            uint8_t primarybus = word & 0xffU;
            uint8_t secondarybus = (word >> 8) & 0xffU;
            if (primarybus != bus) {
                pci_printf(
                    path,
                    "WARNING: primary bus %u doesn't match this bus\n",
                    primarybus);
            }
            probe_bus(self, secondarybus);
        }
        self->callback(path, venid, devid, baseclass, subclass, self->data);
    }
}

// NOLINTNEXTLINE(misc-no-recursion)
static void probe_bus(struct probecontext *self, uint8_t bus) {
    for (uint8_t device = 0; device < 32; device++) {
        probe_dev(self, bus, device);
    }
}

void pci_probe_bus(
    void (*callback)(pcipath path, uint16_t venid, uint16_t devid, uint8_t baseclass, uint8_t subclass, void *data),
    void *data
) {
    struct probecontext ctx;
    ctx.callback = callback;
    ctx.data = data;
    bool ismultifuncdev = pci_read_config_header_type(pci_makepath(0, 0, 0)) & 0x80;
    int maxbuscount = 1;
    if (ismultifuncdev) {
        maxbuscount = 8;
    }
    for (int bus = 0; bus < maxbuscount; bus++) {
        uint16_t venid;
        uint16_t devid;
        pci_readvendevid(&venid, &devid, pci_makepath(0, 0, bus));
        if (venid == 0xffff) {
            continue;
        }
        uint8_t baseclass;
        uint8_t subclass;
        pci_readclass(&baseclass, &subclass, pci_makepath(0, 0, bus));
        if ((baseclass != 0x60) && (subclass != 0x00)) {
            // Not a host bridge
            continue;
        }
        probe_bus(&ctx, bus);
    }
}

void pci_printf(pcipath path, char const *fmt, ...) {
    co_printf("pci(%d,%d,%d): ", pcipath_getbus(path), pcipath_getdev(path), pcipath_getfunc(path));
    va_list ap;
    va_start(ap, fmt);
    co_vprintf(fmt, ap);
    va_end(ap);
}

void pci_readvendevid(uint16_t *venid_out, uint16_t *devid_out, pcipath path) {
    uint32_t word = arch_pci_readconfig(path, 0x00);
    *venid_out = word;
    *devid_out = word >> 16;
}

void pci_readclass(uint8_t *baseclass_out, uint8_t *subclass_out, pcipath path) {
    uint32_t word = arch_pci_readconfig(path, 0x08);
    *baseclass_out = (word >> 24) & 0xffU;
    *subclass_out = (word >> 16) & 0xffU;
}

uint8_t pci_read_config_header_type(pcipath path) {
    uint32_t word = arch_pci_readconfig(path, 0x0c);
    return (word >> 16) & 0xffU;
}


uint8_t pci_readprogif(pcipath path) {
    uint32_t word = arch_pci_readconfig(path, 0x08);
    return (word >> 8) & 0xffU;
}

void pci_writeprogif(pcipath path, uint8_t progif) {
    uint32_t word = arch_pci_readconfig(path, 0x08);
    arch_pci_writeconfig(
        path, 0x08,
        (word & ~(0xffU << 8)) | ((uint32_t)progif << 8));
}

uint8_t pci_readinterruptline(pcipath path) {
    uint32_t word = arch_pci_readconfig(path, 0x3c);
    return word & 0xffU;
}

uint16_t pci_readcmdreg(pcipath path) {
    uint32_t word = arch_pci_readconfig(path, 0x4);
    return word & 0xffffU;
}

void pci_writecmdreg(pcipath path, uint16_t value) {
    // Status register part of the word is either R/O or RWC(read/write-to-clear), so we can just leave
    // those zero and shouldn't affect it.
    arch_pci_writeconfig(path, 0x4, value);
}

uint16_t pci_readstatusreg(pcipath path) {
    uint32_t word = arch_pci_readconfig(path, 0x4);
    return (word >> 16) & 0xffffU;
}

void pci_writestatusreg(pcipath path, uint16_t value) {
    uint32_t word = arch_pci_readconfig(path, 0x4);
    // Make sure we preserve the command register.
    arch_pci_writeconfig(
        path, 0x4, ((uint32_t)value << 16) | (word & 0xffffU));
}

// TODO: Memory BAR support is currently incomplete, because it doesn't query how much address space is used.
WARN_UNUSED_RESULT int pci_readbar(uintptr_t *addr_out, bool *isiobar_out, bool *isprefetchable_out, pcipath path, uint8_t bar) {
    assert(bar <= 5);
    uint8_t regoffset = 0x10 + (bar * 4);
    uint32_t word = arch_pci_readconfig(path, regoffset);
    if ((word & 0x1U) == 0) {
        // Memory space BAR
        uint8_t type = (word >> 1) & 0x3U;
        uintptr_t baseaddr;
        switch(type) {
            case 0x0:
            case 0x1:
                // 32-bit and 16-bit memory space BAR
                baseaddr = word & ~0xfU;
                break;
            case 0x2:
                pci_printf(
                    path, "BAR%d: 64-bit BAR is not supported", bar);
                return -EIO;
            default:
                pci_printf(
                    path, "BAR%d: unrecognized BAR type %d", bar, type);
                return -EIO;
        }
        *addr_out = baseaddr;
        *isiobar_out = false;
        *isprefetchable_out = word & (1U << 3);
    } else {
        // I/O space BAR
        uint32_t baseaddr = word & ~0x3U;
        *addr_out = baseaddr;
        *isiobar_out = true;
        *isprefetchable_out = false;
    }
    return 0;
}

WARN_UNUSED_RESULT int pci_readmembar(uintptr_t *addr_out, bool *isprefetchable_out, pcipath path, uint8_t bar) {
    bool isiobar;
    int ret = pci_readbar(
        addr_out, &isiobar, isprefetchable_out, path, bar);
    if (ret < 0) {
        return ret;
    }
    if (isiobar) {
        pci_printf(path, "BAR%d: expected memory BAR, got I/O BAR", path, bar);
        return -EIO;
    }
    return 0;
}

WARN_UNUSED_RESULT int pci_readiobar(uintptr_t *addr_out, pcipath path, uint8_t bar) {
    bool isiobar, isprefetchable_out;
    int ret = pci_readbar(
        addr_out, &isiobar,
        &isprefetchable_out, path, bar);
    if (ret < 0) {
        return ret;
    }
    if (!isiobar) {
        pci_printf(path, "BAR%d: expected I/O BAR, got memory BAR", bar);
        return -EIO;
    }
    return 0;
}

static void printcallback(pcipath path, uint16_t venid, uint16_t devid, uint8_t baseclass, uint8_t subclass, void *data) {
    (void)data;
    pci_printf(path, "%04x:%04x class %02x:%02x\n", venid, devid, baseclass, subclass);
}

void pci_printbus(void) {
    pci_probe_bus(printcallback, NULL);
}
