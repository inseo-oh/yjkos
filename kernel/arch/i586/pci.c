#include "ioport.h"
#include <assert.h>
#include <kernel/arch/pci.h>
#include <kernel/dev/pci.h>
#include <stdint.h>

static uint16_t CONFIG_ADDRESS_PORT  = 0xcf8;
static uint16_t CONFIG_DATA_PORT     = 0xcfc;

static uint32_t makeconfigaddr(PCIPATH path, uint8_t offset) {
    return (1U << 31) | (((uint32_t)pcipath_get_bus(path)) << 16) | (((uint32_t)pcipath_get_device(path)) << 11) | (((uint32_t)pcipath_get_func(path)) << 8) | offset;
}

uint32_t arch_pci_read_config(PCIPATH path, uint8_t offset) {
    assert(offset % 4 == 0);
    archi586_out32(CONFIG_ADDRESS_PORT, makeconfigaddr(path, offset));
    return archi586_in32(CONFIG_DATA_PORT);
}

void arch_pci_write_config(PCIPATH path, uint8_t offset, uint32_t word) {
    assert(offset % 4 == 0);
    archi586_out32(CONFIG_ADDRESS_PORT, makeconfigaddr(path, offset));
    archi586_out32(CONFIG_DATA_PORT, word);
}
