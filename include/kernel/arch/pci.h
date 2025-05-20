#pragma once
#include <kernel/dev/pci.h>
#include <stdint.h>

uint32_t arch_pci_read_config(PCIPATH path, uint8_t offset);
void arch_pci_write_config(PCIPATH path, uint8_t offset, uint32_t word);
