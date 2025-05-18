#include "ioport.h"
#include <assert.h>
#include <kernel/arch/pci.h>
#include <kernel/dev/pci.h>
#include <stdint.h>

static uint16_t CONFIG_ADDRESS_PORT  = 0xcf8;
static uint16_t CONFIG_DATA_PORT     = 0xcfc;

static uint32_t makeconfigaddr(PCIPATH path, uint8_t offset) {
    return (1U << 31) | (((uint32_t)PciPath_GetBus(path)) << 16) | (((uint32_t)PciPath_GetDevice(path)) << 11) | (((uint32_t)PciPath_GetFunc(path)) << 8) | offset;
}

uint32_t Arch_Pci_ReadConfig(PCIPATH path, uint8_t offset) {
    assert(offset % 4 == 0);
    ArchI586_Out32(CONFIG_ADDRESS_PORT, makeconfigaddr(path, offset));
    return ArchI586_In32(CONFIG_DATA_PORT);
}

void Arch_Pci_WriteConfig(PCIPATH path, uint8_t offset, uint32_t word) {
    assert(offset % 4 == 0);
    ArchI586_Out32(CONFIG_ADDRESS_PORT, makeconfigaddr(path, offset));
    ArchI586_Out32(CONFIG_DATA_PORT, word);
}
