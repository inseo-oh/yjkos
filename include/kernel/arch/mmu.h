#pragma once
#include <kernel/types.h>
#include <kernel/status.h>
#include <kernel/mem/vmm.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

extern const uintptr_t ARCH_KERNEL_SPACE_BASE;
extern const uintptr_t ARCH_KERNEL_IMAGE_ADDRESS_START;
extern const uintptr_t ARCH_KERNEL_IMAGE_ADDRESS_END;
extern const uintptr_t ARCH_KERNEL_VM_START;
extern const uintptr_t ARCH_KERNEL_VM_END;
extern const uintptr_t ARCH_SCRATCH_MAP_BASE;
extern const size_t    ARCH_PAGESIZE;

void arch_mmu_flushtlb_for(void *ptr);
void arch_mmu_flushtlb(void);
FAILABLE_FUNCTION arch_mmu_map(uintptr_t virtaddr, physptr physaddr, size_t pagecount, uint8_t flags, bool useraccess);
FAILABLE_FUNCTION arch_mmu_remap(uintptr_t virtaddr, size_t pagecount, uint8_t flags, bool useraccess);
FAILABLE_FUNCTION arch_mmu_unmap(uintptr_t virtaddr, size_t pagecount);

// Scratch map is useful for quickly mapping physical memory temporaily without going through VMM.
// (But do make sure to disable interrupts while using it, as anyone else can remap it)
// 
// Scratch page is mapped at ARCH_SCRATCH_MAP_BASE.
void arch_mmu_scratchmap(physptr physaddr, bool nocache);

// Emulate full linear->physical address translation, including privilege checks.
FAILABLE_FUNCTION arch_mmu_emulate(physptr *physaddr_out, uintptr_t virtaddr, uint8_t flags, bool isfromuser);
// Emulate linear->physical address translation, without privilege checks.
FAILABLE_FUNCTION arch_mmu_virttophys(physptr *physaddr_out, uintptr_t virtaddr);
