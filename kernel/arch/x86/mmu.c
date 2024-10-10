#include "asm/x86.h"
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
#include <kernel/status.h>
#include <kernel/types.h>
#include <stdint.h>
#include <string.h>

typedef struct pagetable pagetable_t;
struct pagetable {
    archx86_mmuentry entry[ARCHX86_MMU_ENTRY_COUNT];
};
STATIC_ASSERT_TEST(sizeof(pagetable_t) == ARCHX86_MMU_PAGE_SIZE);

#define ENTRY_BIT_MASK   0x3FFUL

#define OFFSET_BIT_OFFSET 0UL
#define OFFSET_BIT_MASK   0xfffUL

#define PTE_BIT_OFFSET   12UL
#define PTE_BIT_MASK     (ENTRY_BIT_MASK << PTE_BIT_OFFSET)

#define PDE_BIT_OFFSET   22UL
#define PDE_BIT_MASK     (ENTRY_BIT_MASK << PDE_BIT_OFFSET)

#define MAKE_VIRTADDR(_pde, _pte, _offset) (((size_t)(_pde) << (size_t)PDE_BIT_OFFSET) | ((size_t)(_pte) << (size_t)PTE_BIT_OFFSET) | ((size_t)(_offset) << (size_t)OFFSET_BIT_OFFSET))

#define PAGEDIR_PD_BASE        MAKE_VIRTADDR(ARCHX86_MMU_PAGEDIR_PDE, ARCHX86_MMU_PAGEDIR_PDE, 0)
#define PAGEDIR_PT_BASE(_pde)  MAKE_VIRTADDR(ARCHX86_MMU_PAGEDIR_PDE, _pde, 0)


static archx86_mmuentry *s_pagedir = (archx86_mmuentry *)PAGEDIR_PD_BASE;
static pagetable_t *s_pagetables = (pagetable_t *)PAGEDIR_PT_BASE(0);

static uintptr_t makevirtaddr(size_t pde, size_t pte, size_t offset) {
    return MAKE_VIRTADDR(pde, pte, offset);
}

const uintptr_t ARCH_KERNEL_SPACE_BASE          = MAKE_VIRTADDR(ARCHX86_MMU_KERNEL_PDE_START, 0, 0);
const uintptr_t ARCH_SCRATCH_MAP_BASE           = MAKE_VIRTADDR(ARCHX86_MMU_SCRATCH_PDE, ARCHX86_MMU_SCRATCH_PTE, 0);
const uintptr_t ARCH_KERNEL_IMAGE_ADDRESS_START = ARCH_KERNEL_VIRTUAL_ADDRESS_BEGIN;
const uintptr_t ARCH_KERNEL_IMAGE_ADDRESS_END   = ARCH_KERNEL_VIRTUAL_ADDRESS_END - 1;
const uintptr_t ARCH_KERNEL_VM_START            = ARCH_KERNEL_IMAGE_ADDRESS_END + 1;
const uintptr_t ARCH_KERNEL_VM_END              = ARCH_SCRATCH_MAP_BASE - 1;
const size_t    ARCH_PAGESIZE                  = ARCHX86_MMU_PAGE_SIZE;

#undef MAKE_VIRTADDR

static size_t pdeindex(uintptr_t addr) {
    return (addr & PDE_BIT_MASK) >> PDE_BIT_OFFSET;
}
static size_t pteindex(uintptr_t addr) {
    return (addr & PTE_BIT_MASK) >> PTE_BIT_OFFSET;
}

void arch_mmu_flushtlb_for(void *ptr) {
    archx86_invlpg(ptr);
}

void arch_mmu_flushtlb(void) {
    archx86_reloadcr3();
}

FAILABLE_FUNCTION arch_mmu_emulate(physptr_t *physaddr_out, uintptr_t virtaddr, memmapflags_t flags, bool isfromuser) {
FAILABLE_PROLOGUE
    archx86_mmu_emulateresult_t result;
    *physaddr_out = 0;
    if (!(flags & MAP_PROT_READ)) {
        THROW(ERR_PERM);
    }
    if (!archx86_mmu_emulate(&result, virtaddr, flags & MAP_PROT_WRITE, isfromuser)) {
        if (result.faultflags & (ARCHX86_MMU_EMUTRANS_FAULT_FLAG_PDE_MISSING | ARCHX86_MMU_EMUTRANS_FAULT_FLAG_PTE_MISSING)) {
            THROW(ERR_FAULT);
        } else {
            THROW(ERR_PERM);
        }
    }
    *physaddr_out = result.physaddr;
FAILABLE_EPILOGUE_BEGIN
FAILABLE_EPILOGUE_END
}

FAILABLE_FUNCTION arch_mmu_virttophys(physptr_t *physaddr_out, uintptr_t virtaddr) {
FAILABLE_PROLOGUE
    uint16_t pde = pdeindex(virtaddr);
    uint16_t pte = pteindex(virtaddr);
    archx86_mmuentry pd_entry = s_pagedir[pde];
    *physaddr_out = 0;
    if (!(pd_entry & ARCHX86_MMU_PDE_FLAG_P)) {
        THROW(ERR_FAULT);
    }
    archx86_mmuentry pt_entry = s_pagetables[pde].entry[pte];
    if (!(pt_entry & ARCHX86_MMU_PTE_FLAG_P)) {
        THROW(ERR_FAULT);
    }
    *physaddr_out = (pt_entry & ~0xfff) + (virtaddr & 0xfff);
FAILABLE_EPILOGUE_BEGIN
FAILABLE_EPILOGUE_END
}

#define ASSERT_ADDR_VALID(_addr, _count) {                                       \
    assert((_addr) != 0);                                                        \
    assert((_count) <= (UINTPTR_MAX / ARCHX86_MMU_PAGE_SIZE));                   \
    assert((((_count) - 1) * ARCHX86_MMU_PAGE_SIZE) <= (UINTPTR_MAX - (_addr))); \
}

FAILABLE_FUNCTION arch_mmu_map(uintptr_t virtaddr, physptr_t physaddr, size_t pagecount, memmapflags_t flags, bool useraccess) {
FAILABLE_PROLOGUE
    ASSERT_INTERRUPTS_DISABLED();
    bool pdcreated = false;
    ASSERT_ADDR_VALID(virtaddr, pagecount);
    ASSERT_ADDR_VALID(physaddr, pagecount);
    assert(isaligned(physaddr, ARCHX86_MMU_PAGE_SIZE));
    if (!(flags & MAP_PROT_READ)) {
        THROW(ERR_PERM);
    }
    for (size_t i = 0; i < pagecount; i++) {
        uintptr_t currentvirtaddr = virtaddr + (i * ARCHX86_MMU_PAGE_SIZE);
        uint16_t pde = pdeindex(currentvirtaddr);
        archx86_mmuentry pd_entry = s_pagedir[pde];
        if (!(pd_entry & ARCHX86_MMU_PDE_FLAG_P)) {
            // Create new PD
            size_t size = 1;
            physptr_t addr;
            TRY(pmm_alloc(&addr, &size));
            pdcreated = true;
            s_pagedir[pde] = addr | ARCHX86_MMU_PDE_FLAG_P | ARCHX86_MMU_PDE_FLAG_RW | ARCHX86_MMU_PDE_FLAG_US;
            arch_mmu_flushtlb_for(&s_pagetables[pde]);
            memset(&s_pagetables[pde], 0, sizeof(s_pagetables[pde]));
            // Flush TLB just to be safe
            for (size_t i = 0; i < ARCHX86_MMU_ENTRY_COUNT; i++) {
                arch_mmu_flushtlb_for((void *)makevirtaddr(pde, i, 0));
            }
        }
    }
    for (size_t i = 0; i < pagecount; i++) {
        uintptr_t currentvirtaddr = virtaddr + (i * ARCHX86_MMU_PAGE_SIZE);
        uintptr_t currentphysaddr = physaddr + (i * ARCHX86_MMU_PAGE_SIZE);
        uint16_t pde = pdeindex(currentvirtaddr);
        uint16_t pte = pteindex(currentvirtaddr);
        archx86_mmuentry oldpte = s_pagetables[pde].entry[pte];
        bool shouldflush = false;
        if (oldpte & ARCHX86_MMU_PTE_FLAG_P) {
            // See if we need to invalidate old TLB
            if ((oldpte & ARCHX86_MMU_PTE_FLAG_RW) && !(flags & MAP_PROT_WRITE)) {
                shouldflush = true;
            }
            if ((oldpte & ARCHX86_MMU_PTE_FLAG_US) && !useraccess) {
                shouldflush = true;
            }
            physptr_t oldaddr = oldpte & ~0xfff;
            if (oldaddr != currentphysaddr) {
                shouldflush = true;
            }
        }
        s_pagetables[pde].entry[pte] = currentphysaddr | ARCHX86_MMU_PTE_FLAG_P;
        if (flags & MAP_PROT_WRITE) {
            s_pagetables[pde].entry[pte] |= ARCHX86_MMU_PTE_FLAG_RW;
        }
        if (flags & MAP_PROT_NOCACHE) {
            s_pagetables[pde].entry[pte] |= ARCHX86_MMU_PTE_FLAG_PCD;
        }
        if (useraccess) {
            s_pagetables[pde].entry[pte] |= ARCHX86_MMU_PTE_FLAG_US;
        }
        if (shouldflush) {
            arch_mmu_flushtlb_for((void *)virtaddr);
        }
    }
FAILABLE_EPILOGUE_BEGIN
    if (DID_FAIL && pdcreated) {
        // TODO: Clean-up unused PD entries
        tty_printf("todo: clean-up unused pd entries\n");
    } 
FAILABLE_EPILOGUE_END
}

FAILABLE_FUNCTION arch_mmu_remap(uintptr_t virtaddr, size_t pagecount, memmapflags_t flags, bool useraccess) {
FAILABLE_PROLOGUE
    ASSERT_INTERRUPTS_DISABLED();
    ASSERT_ADDR_VALID(virtaddr, pagecount);
    if (!(flags & MAP_PROT_READ)) {
        THROW(ERR_PERM);
    }
    for (size_t i = 0; i < pagecount; i++) {
        uintptr_t currentvirtaddr = virtaddr + (i * ARCHX86_MMU_PAGE_SIZE);
        uint16_t pde = pdeindex(currentvirtaddr);
        uint16_t pte = pteindex(currentvirtaddr);
        archx86_mmuentry pd_entry = s_pagedir[pde];
        if (!(pd_entry & ARCHX86_MMU_PDE_FLAG_P)) {
            THROW(ERR_FAULT);
        }
        archx86_mmuentry oldpte = s_pagetables[pde].entry[pte];
        if (!(oldpte & ARCHX86_MMU_PTE_FLAG_P)) {
            THROW(ERR_FAULT);
        }
    }

    for (size_t i = 0; i < pagecount; i++) {
        uintptr_t current_virtaddr = virtaddr + (i * ARCHX86_MMU_PAGE_SIZE);
        uint16_t pde = pdeindex(current_virtaddr);
        uint16_t pte = pteindex(current_virtaddr);
        archx86_mmuentry oldpte = s_pagetables[pde].entry[pte];
        bool should_flush = false;
        // See if we need to invalidate old TLB
        if ((oldpte & ARCHX86_MMU_PTE_FLAG_RW) && !(flags & MAP_PROT_WRITE)) {
            should_flush = true;
        }
        if ((oldpte & ARCHX86_MMU_PTE_FLAG_US) && !useraccess) {
            should_flush = true;
        }
        s_pagetables[pde].entry[pte] &= ~(0xfff & (~ARCHX86_MMU_COMMON_FLAG_P));
        if (flags & MAP_PROT_WRITE) {
            s_pagetables[pde].entry[pte] |= ARCHX86_MMU_PTE_FLAG_RW;
        }
        if (flags & MAP_PROT_NOCACHE) {
            s_pagetables[pde].entry[pte] |= ARCHX86_MMU_PTE_FLAG_PCD;
        }
        if (useraccess) {
            s_pagetables[pde].entry[pte] |= ARCHX86_MMU_PTE_FLAG_US;
        }
        if (should_flush) {
            arch_mmu_flushtlb_for((void *)virtaddr);
        }
    }
FAILABLE_EPILOGUE_BEGIN
FAILABLE_EPILOGUE_END
}

FAILABLE_FUNCTION arch_mmu_unmap(uintptr_t virtaddr, size_t pagecount) {
FAILABLE_PROLOGUE
    ASSERT_INTERRUPTS_DISABLED();
    ASSERT_ADDR_VALID(virtaddr, pagecount);
    for (size_t i = 0; i < pagecount; i++) {
        uintptr_t current_virtaddr = virtaddr + (i * ARCHX86_MMU_PAGE_SIZE);
        uint16_t pde = pdeindex(current_virtaddr);
        uint16_t pte = pteindex(current_virtaddr);
        archx86_mmuentry pd_entry = s_pagedir[pde];
        if (!(pd_entry & ARCHX86_MMU_PDE_FLAG_P)) {
            THROW(ERR_FAULT);
        }
        archx86_mmuentry oldpte = s_pagetables[pde].entry[pte];
        if (!(oldpte & ARCHX86_MMU_PTE_FLAG_P)) {
            THROW(ERR_FAULT);
        }
    }
    for (size_t i = 0; i < pagecount; i++) {
        uintptr_t current_virtaddr = virtaddr + (i * ARCHX86_MMU_PAGE_SIZE);
        uint16_t pde = pdeindex(current_virtaddr);
        uint16_t pte = pteindex(current_virtaddr);
        s_pagetables[pde].entry[pte] = 0;
        arch_mmu_flushtlb_for((void *)current_virtaddr);
    }
    // TODO: Clean-up unused PD entries
FAILABLE_EPILOGUE_BEGIN
////////////////////////////////////////////////////////////////////////////////
FAILABLE_EPILOGUE_END
}

STATIC_ASSERT_TEST(ARCHX86_MMU_SCRATCH_PDE == (ARCHX86_MMU_KERNEL_PDE_START + ARCHX86_MMU_KERNEL_PDE_COUNT - 1));

void arch_mmu_scratchmap(physptr_t physaddr, bool nocache) {
    ASSERT_INTERRUPTS_DISABLED();
    assert(isaligned(physaddr, ARCHX86_MMU_PAGE_SIZE));
    uint16_t pde = ARCHX86_MMU_SCRATCH_PDE;
    uint16_t pte = ARCHX86_MMU_SCRATCH_PTE;
    archx86_mmuentry pd_entry = s_pagedir[pde];
    assert(pd_entry & ARCHX86_MMU_PDE_FLAG_P);
    archx86_mmuentry oldpte = s_pagetables[pde].entry[pte];
    bool should_flush = false;
    physptr_t oldaddr = 0;
    if (oldpte & ARCHX86_MMU_PTE_FLAG_P) {
        // See if we need to invalidate old TLB
        if (oldpte & ARCHX86_MMU_PTE_FLAG_US) {
            should_flush = true;
        }
        oldaddr = oldpte & ~0xfff;
        if (oldaddr != physaddr) {
            should_flush = true;
        }
    }
    s_pagetables[pde].entry[pte] = physaddr | ARCHX86_MMU_PTE_FLAG_P | ARCHX86_MMU_PTE_FLAG_RW;
    if (nocache) {
        s_pagetables[pde].entry[pte] |= ARCHX86_MMU_PTE_FLAG_PCD;
    }
    if (should_flush) {
        arch_mmu_flushtlb_for((void *)ARCH_SCRATCH_MAP_BASE);
    }
}

//------------------------------------------------------------------------------
// Internal API
//------------------------------------------------------------------------------

bool archx86_mmu_emulate(archx86_mmu_emulateresult_t *result_out, uintptr_t virtaddr, bool iswrite, bool isfromuser) {
    uint16_t pde = pdeindex(virtaddr);
    uint16_t pte = pteindex(virtaddr);
    bool ok = true;
    archx86_mmuentry pd_entry = s_pagedir[pde];
    result_out->faultflags = 0;
    result_out->physaddr = 0;
    if (!(pd_entry & ARCHX86_MMU_PDE_FLAG_P)) {
        result_out->faultflags |= ARCHX86_MMU_EMUTRANS_FAULT_FLAG_PDE_MISSING;
        ok = false;
    }
    if (!(pd_entry & ARCHX86_MMU_PDE_FLAG_RW) && iswrite) {
        result_out->faultflags |= ARCHX86_MMU_EMUTRANS_FAULT_FLAG_PDE_WRITE;
        ok = false;
    }
    if (!(pd_entry & ARCHX86_MMU_PDE_FLAG_US) && isfromuser) {
        result_out->faultflags |= ARCHX86_MMU_EMUTRANS_FAULT_FLAG_PDE_USER;
        ok = false;
    }
    if (!ok) {
        goto fail;
    }
    archx86_mmuentry pt_entry = s_pagetables[pde].entry[pte];
    if (!(pt_entry & ARCHX86_MMU_PTE_FLAG_P)) {
        result_out->faultflags |= ARCHX86_MMU_EMUTRANS_FAULT_FLAG_PTE_MISSING;
        ok = false;
    }
    if (!(pt_entry & ARCHX86_MMU_PTE_FLAG_RW) && iswrite) {
        result_out->faultflags |= ARCHX86_MMU_EMUTRANS_FAULT_FLAG_PTE_WRITE;
        ok = false;
    }
    if (!(pt_entry & ARCHX86_MMU_PTE_FLAG_US) && isfromuser) {
        result_out->faultflags |= ARCHX86_MMU_EMUTRANS_FAULT_FLAG_PTE_USER;
        ok = false;
    }
    if (!ok) {
        goto fail;
    }
    result_out->physaddr = pt_entry & ~0xfff;
    return true;
fail:
    return false;
}

void archx86_mmu_writeprotect_kerneltext(void) {
    status_t status = arch_mmu_remap(ARCHX86_ARCH_KERNEL_TEXT_BEGIN, (ARCHX86_ARCH_KERNEL_TEXT_END - ARCHX86_ARCH_KERNEL_TEXT_BEGIN) / ARCHX86_MMU_PAGE_SIZE, MAP_PROT_READ, false);
    (void)status;
    assert(status == OK);
}

void archx86_writeprotect_afterearlyinit(void) {
    status_t status = arch_mmu_remap(ARCHX86_KERNEL_RO_AFTER_EARLY_INIT_BEGIN, (ARCHX86_KERNEL_RO_AFTER_EARLY_INIT_END - ARCHX86_KERNEL_RO_AFTER_EARLY_INIT_BEGIN) / ARCHX86_MMU_PAGE_SIZE, MAP_PROT_READ, false);
    (void)status;
    assert(status == OK);
}

extern const void *archx86_stackbottomtrap;

void archx86_mmu_init(void) {
    // Unmap lower 2MB area
    // for (size_t i = 0; i < ARCHX86_MMU_ENTRY_COUNT; i++) {
    //     s_pagetables[0].entry[i] = 0;
    //     arch_mmu_flushtlb_for((void *)makevirtaddr(0, i, 0));
    // }
    // Setup "stack bottom trap", which basically forces system to triple-fault immediately when kernel
    // runs out of stack memory.
    status_t status = arch_mmu_unmap((uintptr_t)&archx86_stackbottomtrap, 1);
    (void)status;
    assert(status == OK);
    // Unmap kernel VM region
    status = arch_mmu_unmap(ARCH_KERNEL_VM_START, (ARCH_KERNEL_VM_END - ARCH_KERNEL_VM_START + 1) / ARCHX86_MMU_PAGE_SIZE);
    (void)status;
    assert(status == OK);
}
