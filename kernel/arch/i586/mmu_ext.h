#pragma once

// This file mostly uses #define(other than C types and functions) to share with assembly code.

#define ARCHI586_MMU_COMMON_FLAG_P     (1 << 0)
#define ARCHI586_MMU_COMMON_FLAG_RW    (1 << 1)
#define ARCHI586_MMU_COMMON_FLAG_US    (1 << 2)
#define ARCHI586_MMU_COMMON_FLAG_PWT   (1 << 3)
#define ARCHI586_MMU_COMMON_FLAG_PCD   (1 << 4)
#define ARCHI586_MMU_COMMON_FLAG_A     (1 << 5)
#define ARCHI586_MMU_COMMON_FLAG_D     (1 << 6)
#define ARCHI586_MMU_COMMON_FLAG_G     (1 << 8)


#define ARCHI586_MMU_PDE_FLAG_P     ARCHI586_MMU_COMMON_FLAG_P
#define ARCHI586_MMU_PDE_FLAG_RW    ARCHI586_MMU_COMMON_FLAG_RW
#define ARCHI586_MMU_PDE_FLAG_US    ARCHI586_MMU_COMMON_FLAG_US
#define ARCHI586_MMU_PDE_FLAG_PWT   ARCHI586_MMU_COMMON_FLAG_PWT
#define ARCHI586_MMU_PDE_FLAG_PCD   ARCHI586_MMU_COMMON_FLAG_PCD
#define ARCHI586_MMU_PDE_FLAG_A     ARCHI586_MMU_COMMON_FLAG_A
#define ARCHI586_MMU_PDE_FLAG_D     ARCHI586_MMU_COMMON_FLAG_D
#define ARCHI586_MMU_PDE_FLAG_PS    (1 << 7)
#define ARCHI586_MMU_PDE_FLAG_G     ARCHI586_MMU_COMMON_FLAG_G


#define ARCHI586_MMU_PTE_FLAG_P     ARCHI586_MMU_COMMON_FLAG_P
#define ARCHI586_MMU_PTE_FLAG_RW    ARCHI586_MMU_COMMON_FLAG_RW
#define ARCHI586_MMU_PTE_FLAG_US    ARCHI586_MMU_COMMON_FLAG_US
#define ARCHI586_MMU_PTE_FLAG_PWT   ARCHI586_MMU_COMMON_FLAG_PWT
#define ARCHI586_MMU_PTE_FLAG_PCD   ARCHI586_MMU_COMMON_FLAG_PCD
#define ARCHI586_MMU_PTE_FLAG_A     ARCHI586_MMU_COMMON_FLAG_A
#define ARCHI586_MMU_PTE_FLAG_D     ARCHI586_MMU_COMMON_FLAG_D
#define ARCHI586_MMU_PTE_FLAG_PAT   (1 << 7)
#define ARCHI586_MMU_PTE_FLAG_G     ARCHI586_MMU_COMMON_FLAG_G

#define ARCHI586_MMU_PAGE_SIZE        4096
#define ARCHI586_MMU_ENTRY_SIZE       4
#define ARCHI586_MMU_ENTRY_COUNT      1024

#define ARCHI586_MMU_KERNEL_PDE_START    768
#define ARCHI586_MMU_KERNEL_PDE_COUNT    (ARCHI586_MMU_ENTRY_COUNT - ARCHI586_MMU_KERNEL_PDE_START - 1)

#define ARCHI586_MMU_SCRATCH_PDE    (ARCHI586_MMU_KERNEL_PDE_START + ARCHI586_MMU_KERNEL_PDE_COUNT - 1)
#define ARCHI586_MMU_SCRATCH_PTE    (ARCHI586_MMU_ENTRY_COUNT - 1)

#define ARCHI586_MMU_MAX_MEMORY_PER_PTE  ARCHI586_MMU_PAGE_SIZE
#define ARCHI586_MMU_MAX_MEMORY_PER_PDE  (ARCHI586_MMU_MAX_MEMORY_PER_PTE * ARCHI586_MMU_ENTRY_COUNT)
#define ARCHI586_MMU_KERNEL_AREA_SIZE    (ARCHI586_MMU_MAX_MEMORY_PER_PDE * ARCHI586_MMU_KERNEL_PDE_COUNT)

// PDE for recursive mapping the PD itself
#define ARCHI586_MMU_PAGEDIR_PDE            (ARCHI586_MMU_ENTRY_COUNT - 1)

//------------------------------------------------------------------------------
// Below are only applicable to C
//------------------------------------------------------------------------------

#ifndef YJKERNEL_ASMFILE

#include <kernel/arch/mmu.h>
#include <kernel/lib/diagnostics.h>
#include <kernel/types.h>
#include <stdbool.h>
#include <stdint.h>

STATIC_ASSERT_TEST(sizeof(uint32_t) == ARCHI586_MMU_ENTRY_SIZE);
STATIC_ASSERT_TEST(ARCHI586_MMU_ENTRY_COUNT == (ARCHI586_MMU_PAGE_SIZE / ARCHI586_MMU_ENTRY_SIZE));

static uint8_t const ARCHI586_MMU_EMUTRANS_FAULT_FLAG_PDE_MISSING = 1 << 0;
static uint8_t const ARCHI586_MMU_EMUTRANS_FAULT_FLAG_PDE_WRITE   = 1 << 1;
static uint8_t const ARCHI586_MMU_EMUTRANS_FAULT_FLAG_PDE_USER    = 1 << 2;
static uint8_t const ARCHI586_MMU_EMUTRANS_FAULT_FLAG_PTE_MISSING = 1 << 3;
static uint8_t const ARCHI586_MMU_EMUTRANS_FAULT_FLAG_PTE_WRITE   = 1 << 4;
static uint8_t const ARCHI586_MMU_EMUTRANS_FAULT_FLAG_PTE_USER    = 1 << 5;

struct archi586_mmu_emulateresult {
    physptr physaddr;
    uint8_t faultflags; // See ARCHI586_MMU_EMUTRANS_*
};

void archi586_mmu_writeprotect_kerneltext(void);
void archi586_writeprotect_afterearlyinit(void);
void archi586_mmu_init(void);
void archi586_mmu_setupstackbottomtrap(void);

#endif
