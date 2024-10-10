#include "ioport.h"
#include <assert.h>
#include <kernel/arch/pci.h>
#include <kernel/dev/pci.h>
#include <stdint.h>

static archx86_ioaddr_t CONFIG_ADDRESS_PORT  = 0xcf8;
static archx86_ioaddr_t CONFIG_DATA_PORT     = 0xcfc;

static uint32_t makeconfigaddr(pcipath_t path, uint8_t offset) {
    return (1UL << 31) | (((uint32_t)pcipath_getbus(path)) << 16) | (((uint32_t)pcipath_getdev(path)) << 11) | (((uint32_t)pcipath_getfunc(path)) << 8) | offset;
}

uint32_t arch_pci_readconfig(pcipath_t path, uint8_t offset) {
    assert(offset % 4 == 0);
    archx86_out32(CONFIG_ADDRESS_PORT, makeconfigaddr(path, offset));
    return archx86_in32(CONFIG_DATA_PORT);
}

void arch_pci_writeconfig(pcipath_t path, uint8_t offset, uint32_t word) {
    assert(offset % 4 == 0);
    archx86_out32(CONFIG_ADDRESS_PORT, makeconfigaddr(path, offset));
    archx86_out32(CONFIG_DATA_PORT, word);
}
