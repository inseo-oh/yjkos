#include "asm/i586.h"
#include "mmu_ext.h"
#include "sections.h"
#include <kernel/arch/interrupts.h>
#include <kernel/arch/mmu.h>
#include <kernel/io/tty.h>
#include <kernel/lib/diagnostics.h>
#include <kernel/lib/miscmath.h>
#include <kernel/mem/pmm.h>
#include <kernel/mem/vmm.h>
#include <kernel/panic.h>
#include <kernel/types.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>

struct pagetable {
    uint32_t entry[ARCHI586_MMU_ENTRY_COUNT];
};
STATIC_ASSERT_TEST(sizeof(struct pagetable) == ARCHI586_MMU_PAGE_SIZE);

#define ENTRY_BIT_MASK   0x3FFUL

#define OFFSET_BIT_OFFSET 0UL
#define OFFSET_BIT_MASK   0xfffUL

#define PTE_BIT_OFFSET   12UL
#define PTE_BIT_MASK     (ENTRY_BIT_MASK << PTE_BIT_OFFSET)

#define PDE_BIT_OFFSET   22UL
#define PDE_BIT_MASK     (ENTRY_BIT_MASK << PDE_BIT_OFFSET)

#define MAKE_VIRTADDR(_pde, _pte, _offset) (((size_t)(_pde) << (size_t)PDE_BIT_OFFSET) | ((size_t)(_pte) << (size_t)PTE_BIT_OFFSET) | ((size_t)(_offset) << (size_t)OFFSET_BIT_OFFSET))

#define PAGEDIR_PD_BASE        MAKE_VIRTADDR(ARCHI586_MMU_PAGEDIR_PDE, ARCHI586_MMU_PAGEDIR_PDE, 0)
#define PAGEDIR_PT_BASE(_pde)  MAKE_VIRTADDR(ARCHI586_MMU_PAGEDIR_PDE, _pde, 0)


static uint32_t *s_pagedir = (uint32_t *)PAGEDIR_PD_BASE;
static struct pagetable *s_pagetables = (struct pagetable *)PAGEDIR_PT_BASE(0);

static uintptr_t makevirtaddr(size_t pde, size_t pte, size_t offset) {
    return MAKE_VIRTADDR(pde, pte, offset);
}

const uintptr_t ARCH_KERNEL_SPACE_BASE          = MAKE_VIRTADDR(ARCHI586_MMU_KERNEL_PDE_START, 0, 0);
const uintptr_t ARCH_SCRATCH_MAP_BASE           = MAKE_VIRTADDR(ARCHI586_MMU_SCRATCH_PDE, ARCHI586_MMU_SCRATCH_PTE, 0);
const uintptr_t ARCH_KERNEL_IMAGE_ADDRESS_START = ARCH_KERNEL_VIRTUAL_ADDRESS_BEGIN;
const uintptr_t ARCH_KERNEL_IMAGE_ADDRESS_END   = ARCH_KERNEL_VIRTUAL_ADDRESS_END - 1;
const uintptr_t ARCH_KERNEL_VM_START            = ARCH_KERNEL_IMAGE_ADDRESS_END + 1;
const uintptr_t ARCH_KERNEL_VM_END              = ARCH_SCRATCH_MAP_BASE - 1;
const size_t    ARCH_PAGESIZE                  = ARCHI586_MMU_PAGE_SIZE;

#undef MAKE_VIRTADDR

static size_t pdeindex(uintptr_t addr) {
    return (addr & PDE_BIT_MASK) >> PDE_BIT_OFFSET;
}
static size_t pteindex(uintptr_t addr) {
    return (addr & PTE_BIT_MASK) >> PTE_BIT_OFFSET;
}

void arch_mmu_flushtlb_for(void *ptr) {
    archi586_invlpg(ptr);
}

void arch_mmu_flushtlb(void) {
    archi586_reloadcr3();
}

WARN_UNUSED_RESULT int arch_mmu_emulate(physptr *physaddr_out, uintptr_t virtaddr, uint8_t flags, bool isfromuser) {
    uint16_t pde = pdeindex(virtaddr);
    uint16_t pte = pteindex(virtaddr);
    bool iswrite = flags & MAP_PROT_WRITE;
    uint32_t pd_entry = s_pagedir[pde];
    if (!(pd_entry & ARCHI586_MMU_PDE_FLAG_P)) {
        return -EFAULT;
    }
    if (!(pd_entry & ARCHI586_MMU_PDE_FLAG_RW) && iswrite) {
        return -EPERM;
    }
    if (!(pd_entry & ARCHI586_MMU_PDE_FLAG_US) && isfromuser) {
        return -EPERM;
    }
    uint32_t pt_entry = s_pagetables[pde].entry[pte];
    if (!(pt_entry & ARCHI586_MMU_PTE_FLAG_P)) {
        return -EFAULT;
    }
    if (!(pt_entry & ARCHI586_MMU_PTE_FLAG_RW) && iswrite) {
        return -EPERM;
    }
    if (!(pt_entry & ARCHI586_MMU_PTE_FLAG_US) && isfromuser) {
        return -EPERM;
    }
    *physaddr_out = pt_entry & ~0xfff;
    return 0;
}

WARN_UNUSED_RESULT int arch_mmu_virttophys(physptr *physaddr_out,
    uintptr_t virtaddr)
{
    uint16_t pde = pdeindex(virtaddr);
    uint16_t pte = pteindex(virtaddr);
    uint32_t pd_entry = s_pagedir[pde];
    *physaddr_out = 0;
    if (!(pd_entry & ARCHI586_MMU_PDE_FLAG_P)) {
        return -EFAULT;
    }
    uint32_t pt_entry = s_pagetables[pde].entry[pte];
    if (!(pt_entry & ARCHI586_MMU_PTE_FLAG_P)) {
        return -EFAULT;
    }
    *physaddr_out = (pt_entry & ~0xfff) + (virtaddr & 0xfff);
    return 0;
}

#define ASSERT_ADDR_VALID(_addr, _count) {                     \
    assert((_addr) != 0);                                      \
    assert((_count) <= (UINTPTR_MAX / ARCHI586_MMU_PAGE_SIZE)); \
    assert((((_count) - 1) * ARCHI586_MMU_PAGE_SIZE) <=         \
        (UINTPTR_MAX - (_addr)));                              \
}

WARN_UNUSED_RESULT int arch_mmu_map(
    uintptr_t virtaddr, physptr physaddr, size_t pagecount, uint8_t flags,
    bool useraccess)
{
    ASSERT_INTERRUPTS_DISABLED();
    int result = 0;
    bool pdcreated = false;
    ASSERT_ADDR_VALID(virtaddr, pagecount);
    ASSERT_ADDR_VALID(physaddr, pagecount);
    assert(isaligned(physaddr, ARCHI586_MMU_PAGE_SIZE));
    if (!(flags & MAP_PROT_READ)) {
        result = -EPERM;
        goto fail;
    }
    for (size_t i = 0; i < pagecount; i++) {
        uintptr_t currentvirtaddr = virtaddr + (i * ARCHI586_MMU_PAGE_SIZE);
        uint16_t pde = pdeindex(currentvirtaddr);
        uint32_t pd_entry = s_pagedir[pde];
        if (!(pd_entry & ARCHI586_MMU_PDE_FLAG_P)) {
            // Create new PD
            size_t size = 1;
            physptr addr = pmm_alloc(&size);
            if (addr == PHYSICALPTR_NULL) {
                result = -ENOMEM;
                goto fail;
            }
            pdcreated = true;
            s_pagedir[pde] = addr | ARCHI586_MMU_PDE_FLAG_P | ARCHI586_MMU_PDE_FLAG_RW | ARCHI586_MMU_PDE_FLAG_US;
            arch_mmu_flushtlb_for(&s_pagetables[pde]);
            memset(&s_pagetables[pde], 0, sizeof(s_pagetables[pde]));
            // Flush TLB just to be safe
            for (size_t i = 0; i < ARCHI586_MMU_ENTRY_COUNT; i++) {
                arch_mmu_flushtlb_for((void *)makevirtaddr(pde, i, 0));
            }
        }
    }
    for (size_t i = 0; i < pagecount; i++) {
        uintptr_t currentvirtaddr = virtaddr + (i * ARCHI586_MMU_PAGE_SIZE);
        uintptr_t currentphysaddr = physaddr + (i * ARCHI586_MMU_PAGE_SIZE);
        uint16_t pde = pdeindex(currentvirtaddr);
        uint16_t pte = pteindex(currentvirtaddr);
        uint32_t oldpte = s_pagetables[pde].entry[pte];
        bool shouldflush = false;
        if (oldpte & ARCHI586_MMU_PTE_FLAG_P) {
            // See if we need to invalidate old TLB
            if ((oldpte & ARCHI586_MMU_PTE_FLAG_RW) && !(flags & MAP_PROT_WRITE)) {
                shouldflush = true;
            }
            if ((oldpte & ARCHI586_MMU_PTE_FLAG_US) && !useraccess) {
                shouldflush = true;
            }
            physptr oldaddr = oldpte & ~0xfff;
            if (oldaddr != currentphysaddr) {
                shouldflush = true;
            }
        }
        s_pagetables[pde].entry[pte] = currentphysaddr | ARCHI586_MMU_PTE_FLAG_P;
        if (flags & MAP_PROT_WRITE) {
            s_pagetables[pde].entry[pte] |= ARCHI586_MMU_PTE_FLAG_RW;
        }
        if (flags & MAP_PROT_NOCACHE) {
            s_pagetables[pde].entry[pte] |= ARCHI586_MMU_PTE_FLAG_PCD;
        }
        if (useraccess) {
            s_pagetables[pde].entry[pte] |= ARCHI586_MMU_PTE_FLAG_US;
        }
        if (shouldflush) {
            arch_mmu_flushtlb_for((void *)virtaddr);
        }
    }
    goto out;
fail:
    if (pdcreated) {
        // TODO: Clean-up unused PD entries
        tty_printf("todo: clean-up unused pd entries\n");
    } 
out:
    return result;
}

WARN_UNUSED_RESULT int arch_mmu_remap(uintptr_t virtaddr, size_t pagecount, uint8_t flags, bool useraccess) {
    ASSERT_INTERRUPTS_DISABLED();
    ASSERT_ADDR_VALID(virtaddr, pagecount);
    if (!(flags & MAP_PROT_READ)) {
        return -EPERM;
    }
    for (size_t i = 0; i < pagecount; i++) {
        uintptr_t currentvirtaddr = virtaddr + (i * ARCHI586_MMU_PAGE_SIZE);
        uint16_t pde = pdeindex(currentvirtaddr);
        uint16_t pte = pteindex(currentvirtaddr);
        uint32_t pd_entry = s_pagedir[pde];
        if (!(pd_entry & ARCHI586_MMU_PDE_FLAG_P)) {
            return -EFAULT;
        }
        uint32_t oldpte = s_pagetables[pde].entry[pte];
        if (!(oldpte & ARCHI586_MMU_PTE_FLAG_P)) {
            return -EFAULT;
        }
    }

    for (size_t i = 0; i < pagecount; i++) {
        uintptr_t current_virtaddr = virtaddr + (i * ARCHI586_MMU_PAGE_SIZE);
        uint16_t pde = pdeindex(current_virtaddr);
        uint16_t pte = pteindex(current_virtaddr);
        uint32_t oldpte = s_pagetables[pde].entry[pte];
        bool should_flush = false;
        // See if we need to invalidate old TLB
        if ((oldpte & ARCHI586_MMU_PTE_FLAG_RW) && !(flags & MAP_PROT_WRITE)) {
            should_flush = true;
        }
        if ((oldpte & ARCHI586_MMU_PTE_FLAG_US) && !useraccess) {
            should_flush = true;
        }
        s_pagetables[pde].entry[pte] &= ~(0xfff & (~ARCHI586_MMU_COMMON_FLAG_P));
        if (flags & MAP_PROT_WRITE) {
            s_pagetables[pde].entry[pte] |= ARCHI586_MMU_PTE_FLAG_RW;
        }
        if (flags & MAP_PROT_NOCACHE) {
            s_pagetables[pde].entry[pte] |= ARCHI586_MMU_PTE_FLAG_PCD;
        }
        if (useraccess) {
            s_pagetables[pde].entry[pte] |= ARCHI586_MMU_PTE_FLAG_US;
        }
        if (should_flush) {
            arch_mmu_flushtlb_for((void *)virtaddr);
        }
    }
    return 0;
}

WARN_UNUSED_RESULT int arch_mmu_unmap(uintptr_t virtaddr, size_t pagecount) {
    ASSERT_INTERRUPTS_DISABLED();
    ASSERT_ADDR_VALID(virtaddr, pagecount);
    for (size_t i = 0; i < pagecount; i++) {
        uintptr_t current_virtaddr = virtaddr + (i * ARCHI586_MMU_PAGE_SIZE);
        uint16_t pde = pdeindex(current_virtaddr);
        uint16_t pte = pteindex(current_virtaddr);
        uint32_t pd_entry = s_pagedir[pde];
        if (!(pd_entry & ARCHI586_MMU_PDE_FLAG_P)) {
            return -EFAULT;
        }
        uint32_t oldpte = s_pagetables[pde].entry[pte];
        if (!(oldpte & ARCHI586_MMU_PTE_FLAG_P)) {
            return -EFAULT;
        }
    }
    for (size_t i = 0; i < pagecount; i++) {
        uintptr_t current_virtaddr = virtaddr + (i * ARCHI586_MMU_PAGE_SIZE);
        uint16_t pde = pdeindex(current_virtaddr);
        uint16_t pte = pteindex(current_virtaddr);
        s_pagetables[pde].entry[pte] = 0;
        arch_mmu_flushtlb_for((void *)current_virtaddr);
    }
    // TODO: Clean-up unused PD entries
    return true;
}

STATIC_ASSERT_TEST(ARCHI586_MMU_SCRATCH_PDE == (ARCHI586_MMU_KERNEL_PDE_START + ARCHI586_MMU_KERNEL_PDE_COUNT - 1));

void arch_mmu_scratchmap(physptr physaddr, bool nocache) {
    ASSERT_INTERRUPTS_DISABLED();
    assert(isaligned(physaddr, ARCHI586_MMU_PAGE_SIZE));
    uint16_t pde = ARCHI586_MMU_SCRATCH_PDE;
    uint16_t pte = ARCHI586_MMU_SCRATCH_PTE;
    uint32_t pd_entry = s_pagedir[pde];
    assert(pd_entry & ARCHI586_MMU_PDE_FLAG_P);
    uint32_t oldpte = s_pagetables[pde].entry[pte];
    bool should_flush = false;
    physptr oldaddr = 0;
    if (oldpte & ARCHI586_MMU_PTE_FLAG_P) {
        // See if we need to invalidate old TLB
        if (oldpte & ARCHI586_MMU_PTE_FLAG_US) {
            should_flush = true;
        }
        oldaddr = oldpte & ~0xfff;
        if (oldaddr != physaddr) {
            should_flush = true;
        }
    }
    s_pagetables[pde].entry[pte] = physaddr | ARCHI586_MMU_PTE_FLAG_P | ARCHI586_MMU_PTE_FLAG_RW;
    if (nocache) {
        s_pagetables[pde].entry[pte] |= ARCHI586_MMU_PTE_FLAG_PCD;
    }
    if (should_flush) {
        arch_mmu_flushtlb_for((void *)ARCH_SCRATCH_MAP_BASE);
    }
}

//------------------------------------------------------------------------------
// Internal API
//------------------------------------------------------------------------------

void archi586_mmu_writeprotect_kerneltext(void) {
    int ret = arch_mmu_remap(
        ARCHI586_ARCH_KERNEL_TEXT_BEGIN,
        (ARCHI586_ARCH_KERNEL_TEXT_END -
            ARCHI586_ARCH_KERNEL_TEXT_BEGIN) / ARCHI586_MMU_PAGE_SIZE,
        MAP_PROT_READ, false);
    MUST_SUCCEED(ret);
}

void archi586_writeprotect_afterearlyinit(void) {
    int ret = arch_mmu_remap(
        ARCHI586_KERNEL_RO_AFTER_EARLY_INIT_BEGIN,
        (ARCHI586_KERNEL_RO_AFTER_EARLY_INIT_END -
            ARCHI586_KERNEL_RO_AFTER_EARLY_INIT_BEGIN) / ARCHI586_MMU_PAGE_SIZE,
            MAP_PROT_READ, false);
    MUST_SUCCEED(ret);
}

extern const void *archi586_stackbottomtrap;

void archi586_mmu_init(void) {
#if 0
    // Unmap lower 2MB area
    for (size_t i = 0; i < ARCHI586_MMU_ENTRY_COUNT; i++) {
        s_pagetables[0].entry[i] = 0;
        arch_mmu_flushtlb_for((void *)makevirtaddr(0, i, 0));
    }
#endif
    /*
     * Setup "stack bottom trap", which basically forces system to triple-fault
     * immediately when kernel runs out of stack memory.
     */
    int ret = arch_mmu_unmap(
        (uintptr_t)&archi586_stackbottomtrap, 1);
    MUST_SUCCEED(ret);
    // Unmap kernel VM region
    ret = arch_mmu_unmap(
        ARCH_KERNEL_VM_START,
        (ARCH_KERNEL_VM_END - ARCH_KERNEL_VM_START + 1) /
            ARCHI586_MMU_PAGE_SIZE);
    MUST_SUCCEED(ret);
}
