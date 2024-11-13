#pragma once
#include <kernel/lib/diagnostics.h>
#include <kernel/mem/vmm.h>
#include <kernel/types.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

extern void *const ARCH_KERNEL_SPACE_BASE;
extern void *const ARCH_KERNEL_IMAGE_ADDRESS_START;
extern void *const ARCH_KERNEL_IMAGE_ADDRESS_END;
extern void *const ARCH_KERNEL_VM_START;
extern void *const ARCH_KERNEL_VM_END;
extern void *const ARCH_SCRATCH_MAP_BASE;
extern size_t const ARCH_PAGESIZE;

void arch_mmu_flushtlb_for(void *ptr);
void arch_mmu_flushtlb(void);
WARN_UNUSED_RESULT int arch_mmu_map(
    void *virtbase, physptr physbase, size_t pagecount, uint8_t flags,
    bool user_access);
WARN_UNUSED_RESULT int arch_mmu_remap(
    void *virtbase, size_t pagecount, uint8_t flags, bool user_access);
// Returns false if such page does not exist.
WARN_UNUSED_RESULT int arch_mmu_unmap(void *virtbase, size_t pagecount);

/*
 * Scratch map is useful for quickly mapping physical memory temporaily without
 * going through VMM.
 * (But do make sure to disable interrupts while using it, as anyone else can
 * remap it)
 *
 * Scratch page is mapped at ARCH_SCRATCH_MAP_BASE.
 */
// 
void arch_mmu_scratchmap(physptr physaddr, bool nocache);

/*
 * Emulate full linear->physical address translation, including privilege
 * checks.
 */
WARN_UNUSED_RESULT int arch_mmu_emulate(physptr *physaddr_out, void *virt, uint8_t flags, bool isfromuser);

/*
 * Emulate linear->physical address translation, without privilege checks.
 * Returns false if it failed.
 */
WARN_UNUSED_RESULT int arch_mmu_virt_to_phys(physptr *physaddr_out,
    void *virt);
