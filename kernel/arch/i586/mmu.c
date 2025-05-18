#include "asm/i586.h"
#include "mmu_ext.h"
#include "sections.h"
#include <errno.h>
#include <kernel/arch/interrupts.h>
#include <kernel/arch/mmu.h>
#include <kernel/io/co.h>
#include <kernel/lib/diagnostics.h>
#include <kernel/lib/miscmath.h>
#include <kernel/mem/pmm.h>
#include <kernel/mem/vmm.h>
#include <kernel/types.h>
#include <stdint.h>
#include <string.h>

struct pagetable {
    uint32_t entry[ARCHI586_MMU_ENTRY_COUNT];
};
STATIC_ASSERT_TEST(sizeof(struct pagetable) == ARCHI586_MMU_PAGE_SIZE);

#define ENTRY_BIT_MASK 0x3ffU

#define OFFSET_BIT_OFFSET 0
#define OFFSET_BIT_MASK 0xfffU

#define PTE_BIT_OFFSET 12
#define PTE_BIT_MASK (ENTRY_BIT_MASK << PTE_BIT_OFFSET)

#define PDE_BIT_OFFSET 22
#define PDE_BIT_MASK (ENTRY_BIT_MASK << PDE_BIT_OFFSET)

#define MAKE_VIRTADDR(_pde, _pte, _offset)              \
    (((uintptr_t)(_pde) << (uintptr_t)PDE_BIT_OFFSET) | \
     ((uintptr_t)(_pte) << (uintptr_t)PTE_BIT_OFFSET) | \
     ((uintptr_t)(_offset) << (uintptr_t)OFFSET_BIT_OFFSET))

#define PAGEDIR_PD_BASE MAKE_VIRTADDR(ARCHI586_MMU_PAGEDIR_PDE, ARCHI586_MMU_PAGEDIR_PDE, 0)
#define PAGEDIR_PT_BASE(_pde) MAKE_VIRTADDR(ARCHI586_MMU_PAGEDIR_PDE, _pde, 0)

static uint32_t *s_pagedir = (uint32_t *)PAGEDIR_PD_BASE;
static struct pagetable *s_pagetables = (struct pagetable *)PAGEDIR_PT_BASE(0);

#define KERNEL_SPACE_BASE MAKE_VIRTADDR(ARCHI586_MMU_KERNEL_PDE_START, 0, 0)
#define SCRATCH_MAP_BASE MAKE_VIRTADDR(ARCHI586_MMU_SCRATCH_PDE, ARCHI586_MMU_SCRATCH_PTE, 0)
#define KERNEL_IMAGE_ADDRESS_START ARCH_KERNEL_VIRTUAL_ADDRESS_BEGIN
#define KERNEL_IMAGE_ADDRESS_END ((char *)ARCH_KERNEL_VIRTUAL_ADDRESS_END - 1)
#define KERNEL_VM_START (KERNEL_IMAGE_ADDRESS_END + 1)
#define KERNEL_VM_END (SCRATCH_MAP_BASE - 1)

void *const ARCH_KERNEL_SPACE_BASE = (void *)KERNEL_SPACE_BASE;
void *const ARCH_SCRATCH_MAP_BASE = (void *)SCRATCH_MAP_BASE;
void *const ARCH_KERNEL_IMAGE_ADDRESS_START = (void *)KERNEL_IMAGE_ADDRESS_START;
void *const ARCH_KERNEL_IMAGE_ADDRESS_END = (void *)KERNEL_IMAGE_ADDRESS_END;
void *const ARCH_KERNEL_VM_START = (void *)KERNEL_VM_START;
void *const ARCH_KERNEL_VM_END = (void *)KERNEL_VM_END;

size_t const ARCH_PAGESIZE = ARCHI586_MMU_PAGE_SIZE;

static size_t pde_index(void *ptr) {
    return ((uintptr_t)ptr & PDE_BIT_MASK) >> PDE_BIT_OFFSET;
}
static size_t pte_index(void *ptr) {
    return ((uintptr_t)ptr & PTE_BIT_MASK) >> PTE_BIT_OFFSET;
}

void Arch_Mmu_FlushTlbFor(void *ptr) {
    ArchI586_Invlpg(ptr);
}

void Arch_Mmu_FlushTlb(void) {
    ArchI586_ReloadCr3();
}

[[nodiscard]] int Arch_Mmu_Emulate(PHYSPTR *physaddr_out, void *virtaddr, uint8_t flags, MMU_USER_ACCESS is_from_user) {
    uint16_t pde = pde_index(virtaddr);
    uint16_t pte = pte_index(virtaddr);
    bool is_write = flags & MAP_PROT_WRITE;
    uint32_t pd_entry = s_pagedir[pde];
    if (!(pd_entry & ARCHI586_MMU_PDE_FLAG_P)) {
        return -EFAULT;
    }
    if (!(pd_entry & ARCHI586_MMU_PDE_FLAG_RW) && is_write) {
        return -EPERM;
    }
    if (!(pd_entry & ARCHI586_MMU_PDE_FLAG_US) && is_from_user) {
        return -EPERM;
    }
    uint32_t pt_entry = s_pagetables[pde].entry[pte];
    if (!(pt_entry & ARCHI586_MMU_PTE_FLAG_P)) {
        return -EFAULT;
    }
    if (!(pt_entry & ARCHI586_MMU_PTE_FLAG_RW) && is_write) {
        return -EPERM;
    }
    if (!(pt_entry & ARCHI586_MMU_PTE_FLAG_US) && (is_from_user == MMU_USER_ACCESS_YES)) {
        return -EPERM;
    }
    *physaddr_out = pt_entry & ~0xfffU;
    return 0;
}

[[nodiscard]] int Arch_Mmu_VirtToPhys(PHYSPTR *physaddr_out, void *virt) {
    uint16_t pde = pde_index(virt);
    uint16_t pte = pte_index(virt);
    uint32_t pd_entry = s_pagedir[pde];
    *physaddr_out = 0;
    if (!(pd_entry & ARCHI586_MMU_PDE_FLAG_P)) {
        return -EFAULT;
    }
    uint32_t pt_entry = s_pagetables[pde].entry[pte];
    if (!(pt_entry & ARCHI586_MMU_PTE_FLAG_P)) {
        return -EFAULT;
    }
    *physaddr_out = (pt_entry & ~0xfffU) + ((uintptr_t)virt & 0xfffU);
    return 0;
}

#define ASSERT_ADDR_VALID(_addr, _count)                                                                  \
    {                                                                                                     \
        assert((uintptr_t)(_addr) != 0);                                                                  \
        assert((_count) <= (UINTPTR_MAX / ARCHI586_MMU_PAGE_SIZE));                                       \
        assert(!WILL_ADD_OVERFLOW((uintptr_t)(_addr), ((_count) * ARCHI586_MMU_PAGE_SIZE), UINTPTR_MAX)); \
    }

static int create_pd(uint8_t pde) {
    size_t size = 1;
    PHYSPTR addr = Pmm_Alloc(&size);
    if (addr == PHYSICALPTR_NULL) {
        return -ENOMEM;
    }
    s_pagedir[pde] = addr | ARCHI586_MMU_PDE_FLAG_P | ARCHI586_MMU_PDE_FLAG_RW | ARCHI586_MMU_PDE_FLAG_US;
    Arch_Mmu_FlushTlbFor(&s_pagetables[pde]);
    memset(&s_pagetables[pde], 0, sizeof(s_pagetables[pde]));
    /* Flush TLB just to be safe **********************************************/
    for (size_t i = 0; i < ARCHI586_MMU_ENTRY_COUNT; i++) {
        Arch_Mmu_FlushTlbFor((void *)MAKE_VIRTADDR(pde, i, 0));
    }
    return 0;
}

static void map_single_page(void *virt, PHYSPTR phys, uint8_t flags, MMU_USER_ACCESS user_access) {
    uint16_t pde = pde_index(virt);
    uint16_t pte = pte_index(virt);
    uint32_t oldpte = s_pagetables[pde].entry[pte];
    bool shouldflush = false;
    if (oldpte & ARCHI586_MMU_PTE_FLAG_P) {
        /* See if we need to invalidate old TLB *******************************/
        if ((oldpte & ARCHI586_MMU_PTE_FLAG_RW) && !(flags & MAP_PROT_WRITE)) {
            shouldflush = true;
        }
        if ((oldpte & ARCHI586_MMU_PTE_FLAG_US) && user_access == MMU_USER_ACCESS_NO) {
            shouldflush = true;
        }
        PHYSPTR oldaddr = oldpte & ~0xfffU;
        if (oldaddr != phys) {
            shouldflush = true;
        }
    }
    s_pagetables[pde].entry[pte] = phys | ARCHI586_MMU_PTE_FLAG_P;
    if (flags & MAP_PROT_WRITE) {
        s_pagetables[pde].entry[pte] |= ARCHI586_MMU_PTE_FLAG_RW;
    }
    if (flags & MAP_PROT_NOCACHE) {
        s_pagetables[pde].entry[pte] |= ARCHI586_MMU_PTE_FLAG_PCD;
    }
    if (user_access == MMU_USER_ACCESS_YES) {
        s_pagetables[pde].entry[pte] |= ARCHI586_MMU_PTE_FLAG_US;
    }
    if (shouldflush) {
        Arch_Mmu_FlushTlbFor(virt);
    }
}

[[nodiscard]] int Arch_Mmu_Map(void *virt_base, PHYSPTR physbase, size_t page_count, uint8_t flags, MMU_USER_ACCESS user_access) {
    ASSERT_IRQ_DISABLED();
    int ret = 0;
    bool pdcreated = false;
    ASSERT_ADDR_VALID(virt_base, page_count);
    ASSERT_ADDR_VALID(physbase, page_count);
    assert(IsAligned(physbase, ARCHI586_MMU_PAGE_SIZE));
    if (!(flags & MAP_PROT_READ)) {
        ret = -EPERM;
        goto fail;
    }
    for (size_t i = 0; i < page_count; i++) {
        void *current_virt = (char *)virt_base + (i * ARCHI586_MMU_PAGE_SIZE);
        uint16_t pde = pde_index(current_virt);
        uint32_t pd_entry = s_pagedir[pde];
        if ((pd_entry & ARCHI586_MMU_PDE_FLAG_P)) {
            continue;
        }
        /* Create new PD ******************************************************/
        int ret = create_pd(pde);
        if (ret < 0) {
            goto fail;
        }
        pdcreated = true;
    }
    for (size_t i = 0; i < page_count; i++) {
        void *virt = (char *)virt_base + (i * ARCHI586_MMU_PAGE_SIZE);
        PHYSPTR phys = physbase + (i * ARCHI586_MMU_PAGE_SIZE);
        map_single_page(virt, phys, flags, user_access);
    }
    goto out;
fail:
    if (pdcreated) {
        /* TODO: Clean-up unused PD entries */
        Co_Printf("todo: clean-up unused pd entries\n");
    }
out:
    return ret;
}

static int check_presence(void *virt) {
    uint16_t pde = pde_index(virt);
    uint16_t pte = pte_index(virt);
    uint32_t pd_entry = s_pagedir[pde];
    if (!(pd_entry & ARCHI586_MMU_PDE_FLAG_P)) {
        return -EFAULT;
    }
    uint32_t oldpte = s_pagetables[pde].entry[pte];
    if (!(oldpte & ARCHI586_MMU_PTE_FLAG_P)) {
        return -EFAULT;
    }
    return 0;
}

static void remap_single_page(void *virt, uint8_t flags, MMU_USER_ACCESS user_access) {
    uint16_t pde = pde_index(virt);
    uint16_t pte = pte_index(virt);
    uint32_t oldpte = s_pagetables[pde].entry[pte];
    bool should_flush = false;
    /* See if we need to invalidate old TLB ***********************************/
    if ((oldpte & ARCHI586_MMU_PTE_FLAG_RW) && !(flags & MAP_PROT_WRITE)) {
        should_flush = true;
    }
    if ((oldpte & ARCHI586_MMU_PTE_FLAG_US) && user_access == MMU_USER_ACCESS_NO) {
        should_flush = true;
    }
    /* Update the table *******************************************************/
    s_pagetables[pde].entry[pte] &= ~(0xfffU & (~ARCHI586_MMU_COMMON_FLAG_P));
    if (flags & MAP_PROT_WRITE) {
        s_pagetables[pde].entry[pte] |= ARCHI586_MMU_PTE_FLAG_RW;
    }
    if (flags & MAP_PROT_NOCACHE) {
        s_pagetables[pde].entry[pte] |= ARCHI586_MMU_PTE_FLAG_PCD;
    }
    if (user_access) {
        s_pagetables[pde].entry[pte] |= ARCHI586_MMU_PTE_FLAG_US;
    }
    if (should_flush) {
        Arch_Mmu_FlushTlbFor(virt);
    }
}

[[nodiscard]] int Arch_Mmu_Remap(void *virt_base, size_t page_count, uint8_t flags, MMU_USER_ACCESS user_access) {
    ASSERT_IRQ_DISABLED();
    ASSERT_ADDR_VALID(virt_base, page_count);
    if (!(flags & MAP_PROT_READ)) {
        return -EPERM;
    }
    for (size_t i = 0; i < page_count; i++) {
        void *virt = (char *)virt_base + (i * ARCHI586_MMU_PAGE_SIZE);
        int ret = check_presence(virt);
        if (ret < 0) {
            return ret;
        }
    }

    for (size_t i = 0; i < page_count; i++) {
        void *virt = (char *)virt_base + (i * ARCHI586_MMU_PAGE_SIZE);
        remap_single_page(virt, flags, user_access);
    }
    return 0;
}

[[nodiscard]] int Arch_Mmu_Unmap(void *virt_base, size_t page_count) {
    ASSERT_IRQ_DISABLED();
    ASSERT_ADDR_VALID(virt_base, page_count);
    for (size_t i = 0; i < page_count; i++) {
        void *virt = (char *)virt_base + (i * ARCHI586_MMU_PAGE_SIZE);
        int ret = check_presence(virt);
        if (ret < 0) {
            return ret;
        }
    }
    for (size_t i = 0; i < page_count; i++) {
        void *current_virt_base = (char *)virt_base + (i * ARCHI586_MMU_PAGE_SIZE);
        uint16_t pde = pde_index(current_virt_base);
        uint16_t pte = pte_index(current_virt_base);
        s_pagetables[pde].entry[pte] = 0;
        Arch_Mmu_FlushTlbFor(current_virt_base);
    }
    /* TODO: Clean-up unused PD entries */
    return true;
}

STATIC_ASSERT_TEST(ARCHI586_MMU_SCRATCH_PDE == (ARCHI586_MMU_KERNEL_PDE_START + ARCHI586_MMU_KERNEL_PDE_COUNT - 1));

void Arch_Mmu_ScratchMap(PHYSPTR physaddr, MMU_CACHE_INHIBIT cache_inhibit) {
    ASSERT_IRQ_DISABLED();
    assert(IsAligned(physaddr, ARCHI586_MMU_PAGE_SIZE));
    uint16_t pde = ARCHI586_MMU_SCRATCH_PDE;
    uint16_t pte = ARCHI586_MMU_SCRATCH_PTE;
    uint32_t pd_entry = s_pagedir[pde];
    assert(pd_entry & ARCHI586_MMU_PDE_FLAG_P);
    uint32_t oldpte = s_pagetables[pde].entry[pte];
    bool should_flush = false;
    PHYSPTR oldaddr = 0;

    if (oldpte & ARCHI586_MMU_PTE_FLAG_P) {
        /* See if we need to invalidate old TLB *******************************/
        if (oldpte & ARCHI586_MMU_PTE_FLAG_US) {
            should_flush = true;
        }
        oldaddr = oldpte & ~0xfffU;
        if (oldaddr != physaddr) {
            should_flush = true;
        }
    }
    s_pagetables[pde].entry[pte] = physaddr | ARCHI586_MMU_PTE_FLAG_P | ARCHI586_MMU_PTE_FLAG_RW;
    if (cache_inhibit == MMU_CACHE_INHIBIT_YES) {
        s_pagetables[pde].entry[pte] |= ARCHI586_MMU_PTE_FLAG_PCD;
    }
    if (should_flush) {
        Arch_Mmu_FlushTlbFor(ARCH_SCRATCH_MAP_BASE);
    }
}

/*******************************************************************************
 * Internal API
 ******************************************************************************/

void ArchI586_Mmu_WriteProtectKernelText(void) {
    int ret = Arch_Mmu_Remap(
        ARCHI586_ARCH_KERNEL_TEXT_BEGIN,
        (ARCHI586_ARCH_KERNEL_TEXT_END - ARCHI586_ARCH_KERNEL_TEXT_BEGIN) / ARCHI586_MMU_PAGE_SIZE,
        MAP_PROT_READ,
        false);
    MUST_SUCCEED(ret);
}

void ArchI586_Mmu_WriteProtectAfterEarlyInit(void) {
    int ret = Arch_Mmu_Remap(
        ARCHI586_KERNEL_RO_AFTER_EARLY_INIT_BEGIN,
        (ARCHI586_KERNEL_RO_AFTER_EARLY_INIT_END -
         ARCHI586_KERNEL_RO_AFTER_EARLY_INIT_BEGIN) /
            ARCHI586_MMU_PAGE_SIZE,
        MAP_PROT_READ, false);
    MUST_SUCCEED(ret);
}

extern const void *archi586_stackbottomtrap;

void ArchI586_Mmu_Init(void) {
#if 0
    /* Unmap lower 2MB area ***************************************************/
    for (size_t i = 0; i < ARCHI586_MMU_ENTRY_COUNT; i++) {
        s_pagetables[0].entry[i] = 0;
        Arch_Mmu_FlushTlbFor((void *)MAKE_VIRTADDR(0, i, 0));
    }
#endif
    /* Setup "stack bottom trap", which basically forces system to triple-fault immediately when kernel runs out of stack memory. */
    int ret = Arch_Mmu_Unmap(&archi586_stackbottomtrap, 1);
    MUST_SUCCEED(ret);
    /* Unmap kernel VM region */
    ret = Arch_Mmu_Unmap(
        ARCH_KERNEL_VM_START,
        ((uintptr_t)ARCH_KERNEL_VM_END - (uintptr_t)ARCH_KERNEL_VM_START + 1) / ARCHI586_MMU_PAGE_SIZE);
    MUST_SUCCEED(ret);
}
