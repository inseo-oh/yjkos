#pragma once
#include <kernel/dev/pci.h>
#include <stdint.h>

uint32_t arch_pci_readconfig(pcipath_t path, uint8_t offset);
void arch_pci_writeconfig(pcipath_t path, uint8_t offset, uint32_t word);
