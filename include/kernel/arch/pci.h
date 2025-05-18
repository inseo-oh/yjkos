#pragma once
#include <kernel/dev/pci.h>
#include <stdint.h>

uint32_t Arch_Pci_ReadConfig(PCIPATH path, uint8_t offset);
void Arch_Pci_WriteConfig(PCIPATH path, uint8_t offset, uint32_t word);
