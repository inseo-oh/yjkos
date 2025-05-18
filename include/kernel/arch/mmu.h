#pragma once
#include <kernel/lib/diagnostics.h>
#include <kernel/mem/vmm.h>
#include <kernel/types.h>
#include <stddef.h>
#include <stdint.h>

extern void *const ARCH_KERNEL_SPACE_BASE;
extern void *const ARCH_KERNEL_IMAGE_ADDRESS_START;
extern void *const ARCH_KERNEL_IMAGE_ADDRESS_END;
extern void *const ARCH_KERNEL_VM_START;
extern void *const ARCH_KERNEL_VM_END;
extern void *const ARCH_SCRATCH_MAP_BASE;
extern size_t const ARCH_PAGESIZE;

typedef enum {
    MMU_USER_ACCESS_NO,
    MMU_USER_ACCESS_YES,
} MMU_USER_ACCESS;

typedef enum {
    MMU_CACHE_INHIBIT_NO,
    MMU_CACHE_INHIBIT_YES,
} MMU_CACHE_INHIBIT;

void Arch_Mmu_FlushTlbFor(void *ptr);
void Arch_Mmu_FlushTlb(void);
[[nodiscard]] int Arch_Mmu_Map(void *virt_base, PHYSPTR physbase, size_t page_count, uint8_t flags, MMU_USER_ACCESS user_access);
[[nodiscard]] int Arch_Mmu_Remap(void *virt_base, size_t page_count, uint8_t flags, MMU_USER_ACCESS user_access);
/*
 * Returns false if such page does not exist.
 */
[[nodiscard]] int Arch_Mmu_Unmap(void *virt_base, size_t page_count);

/*
 * Scratch map is useful for quickly mapping physical memory temporaily without going through VMM.
 * (But do make sure to disable interrupts while using it, as anyone else can remap it)
 *
 * Scratch page is mapped at ARCH_SCRATCH_MAP_BASE.
 */
void Arch_Mmu_ScratchMap(PHYSPTR phys_addr, MMU_CACHE_INHIBIT cache_inhibit);

/*
 * Emulate full linear->physical address translation, including privilege checks.
 */
[[nodiscard]] int Arch_Mmu_Emulate(PHYSPTR *phys_addr_out, void *virt, uint8_t flags, MMU_USER_ACCESS is_from_user);

/*
 * Emulate linear->physical address translation, without privilege checks.
 */
[[nodiscard]] int Arch_Mmu_VirtToPhys(PHYSPTR *phys_addr_out, void *virt);
