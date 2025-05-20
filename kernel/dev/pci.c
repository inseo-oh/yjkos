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
    return pci_read_config_header_type(path) & 0x7f;
}

PCIPATH pci_make_path(uint8_t bus, uint8_t device, uint8_t function) {
    assert(device < 32);
    assert(function < 8);
    return (((uint32_t)bus) << 8) |
           ((uint32_t)device << 3) |
           (uint32_t)function;
}

uint8_t pcipath_get_bus(PCIPATH path) {
    return path >> 8;
}

uint8_t pcipath_get_device(PCIPATH path) {
    return ((uint32_t)path >> 3) & 0x1fU;
}

uint8_t pcipath_get_func(PCIPATH path) {
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
    pci_read_ven_dev_id(&venid, &devid, pci_make_path(bus, device, 0));
    if (venid == 0xffff) {
        return;
    }
    bool is_multifunc_dev = pci_read_config_header_type(pci_make_path(bus, device, 0)) & 0x80;
    int maxfunccount = 1;
    if (is_multifunc_dev) {
        maxfunccount = 8;
    }
    for (int func = 0; func < maxfunccount; func++) {
        PCIPATH path = pci_make_path(bus, device, func);
        uint16_t venid;
        uint16_t devid;
        pci_read_ven_dev_id(&venid, &devid, path);
        if (venid == 0xffff) {
            continue;
        }

        uint8_t baseclass;
        uint8_t subclass;
        pci_read_class(&baseclass, &subclass, path);
        if ((baseclass == 0x6) && (subclass == 0x4)) {
            if (headertype(path) != HEADERTYPE_PCITOPCI) {
                pci_printf(path, "WARNING: PCI-PCI bridge, but wrong config header type?\n");
            }
            uint32_t word = arch_pci_read_config(path, 0x18);
            uint8_t primarybus = word & 0xffU;
            uint8_t secondarybus = (word >> 8) & 0xffU;
            if (primarybus != bus) {
                pci_printf(path, "WARNING: primary bus %u doesn't match this bus\n", primarybus);
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

void pci_probe_bus(
    void (*callback)(PCIPATH path, uint16_t venid, uint16_t devid, uint8_t baseclass, uint8_t subclass, void *data),
    void *data) {
    struct probecontext ctx;
    ctx.callback = callback;
    ctx.data = data;
    bool is_multifunc_dev = pci_read_config_header_type(pci_make_path(0, 0, 0)) & 0x80;
    int max_bus_count = 1;
    if (is_multifunc_dev) {
        max_bus_count = 8;
    }
    for (int bus = 0; bus < max_bus_count; bus++) {
        uint16_t venid;
        uint16_t devid;
        pci_read_ven_dev_id(&venid, &devid, pci_make_path(0, 0, bus));
        if (venid == 0xffff) {
            continue;
        }
        uint8_t baseclass;
        uint8_t subclass;
        pci_read_class(&baseclass, &subclass, pci_make_path(0, 0, bus));
        if ((baseclass != 0x60) && (subclass != 0x00)) {
            /* Not a host bridge */
            continue;
        }
        probe_bus(&ctx, bus);
    }
}

void pci_printf(PCIPATH path, char const *fmt, ...) {
    co_printf("pci(%d,%d,%d): ", pcipath_get_bus(path), pcipath_get_device(path), pcipath_get_func(path));
    va_list ap;
    va_start(ap, fmt);
    co_vprintf(fmt, ap);
    va_end(ap);
}

void pci_read_ven_dev_id(uint16_t *venid_out, uint16_t *devid_out, PCIPATH path) {
    uint32_t word = arch_pci_read_config(path, 0x00);
    *venid_out = word;
    *devid_out = word >> 16;
}

void pci_read_class(uint8_t *baseclass_out, uint8_t *subclass_out, PCIPATH path) {
    uint32_t word = arch_pci_read_config(path, 0x08);
    *baseclass_out = (word >> 24) & 0xffU;
    *subclass_out = (word >> 16) & 0xffU;
}

uint8_t pci_read_config_header_type(PCIPATH path) {
    uint32_t word = arch_pci_read_config(path, 0x0c);
    return (word >> 16) & 0xffU;
}

uint8_t pci_read_progif(PCIPATH path) {
    uint32_t word = arch_pci_read_config(path, 0x08);
    return (word >> 8) & 0xffU;
}

void pci_write_progif(PCIPATH path, uint8_t progif) {
    uint32_t word = arch_pci_read_config(path, 0x08);
    arch_pci_write_config(path, 0x08, (word & ~(0xffU << 8)) | ((uint32_t)progif << 8));
}

uint8_t pci_read_irq_line(PCIPATH path) {
    uint32_t word = arch_pci_read_config(path, 0x3c);
    return word & 0xffU;
}

uint16_t pci_read_cmd_reg(PCIPATH path) {
    uint32_t word = arch_pci_read_config(path, 0x4);
    return word & 0xffffU;
}

void pci_write_cmd_reg(PCIPATH path, uint16_t value) {
    /* Status register part of the word is either R/O or RWC(read/write-to-clear), so we can just leave those zero and shouldn't affect it. */
    arch_pci_write_config(path, 0x4, value);
}

uint16_t pci_read_status_reg(PCIPATH path) {
    uint32_t word = arch_pci_read_config(path, 0x4);
    return (word >> 16) & 0xffffU;
}

void pci_write_status_reg(PCIPATH path, uint16_t value) {
    uint32_t word = arch_pci_read_config(path, 0x4);
    /* Make sure we preserve the command register. */
    arch_pci_write_config(path, 0x4, ((uint32_t)value << 16) | (word & 0xffffU));
}

/* TODO: Memory BAR support is currently incomplete, because it doesn't query how much address space is used. */
[[nodiscard]] int pci_read_bar(uintptr_t *addr_out, bool *isiobar_out, bool *isprefetchable_out, PCIPATH path, uint8_t bar) {
    assert(bar <= 5);
    uint8_t regoffset = 0x10 + (bar * 4);
    uint32_t word = arch_pci_read_config(path, regoffset);
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
            pci_printf(path, "BAR%d: 64-bit BAR is not supported", bar);
            return -EIO;
        default:
            pci_printf(path, "BAR%d: unrecognized BAR type %d", bar, type);
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

[[nodiscard]] int pci_read_mem_bar(uintptr_t *addr_out, bool *isprefetchable_out, PCIPATH path, int bar) {
    bool isiobar;
    int ret = pci_read_bar(addr_out, &isiobar, isprefetchable_out, path, bar);
    if (ret < 0) {
        return ret;
    }
    if (isiobar) {
        pci_printf(path, "BAR%d: expected memory BAR, got I/O BAR", path, bar);
        return -EIO;
    }
    return 0;
}

[[nodiscard]] int pci_read_io_bar(uintptr_t *addr_out, PCIPATH path, int bar) {
    bool is_iobar, is_prefetchable_out;
    int ret = pci_read_bar(addr_out, &is_iobar, &is_prefetchable_out, path, bar);
    if (ret < 0) {
        return ret;
    }
    if (!is_iobar) {
        pci_printf(path, "BAR%d: expected I/O BAR, got memory BAR", bar);
        return -EIO;
    }
    return 0;
}

static void print_callback(PCIPATH path, uint16_t venid, uint16_t devid, uint8_t baseclass, uint8_t subclass, void *data) {
    (void)data;
    pci_printf(path, "%04x:%04x class %02x:%02x\n", venid, devid, baseclass, subclass);
}

void pci_print_bus(void) {
    pci_probe_bus(print_callback, NULL);
}
