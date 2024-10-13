#pragma once

// This file mostly uses #define(other than C types and functions) to share with assembly code.

#define ARCHX86_MMU_COMMON_FLAG_P     (1 << 0)
#define ARCHX86_MMU_COMMON_FLAG_RW    (1 << 1)
#define ARCHX86_MMU_COMMON_FLAG_US    (1 << 2)
#define ARCHX86_MMU_COMMON_FLAG_PWT   (1 << 3)
#define ARCHX86_MMU_COMMON_FLAG_PCD   (1 << 4)
#define ARCHX86_MMU_COMMON_FLAG_A     (1 << 5)
#define ARCHX86_MMU_COMMON_FLAG_D     (1 << 6)
#define ARCHX86_MMU_COMMON_FLAG_G     (1 << 8)


#define ARCHX86_MMU_PDE_FLAG_P     ARCHX86_MMU_COMMON_FLAG_P
#define ARCHX86_MMU_PDE_FLAG_RW    ARCHX86_MMU_COMMON_FLAG_RW
#define ARCHX86_MMU_PDE_FLAG_US    ARCHX86_MMU_COMMON_FLAG_US
#define ARCHX86_MMU_PDE_FLAG_PWT   ARCHX86_MMU_COMMON_FLAG_PWT
#define ARCHX86_MMU_PDE_FLAG_PCD   ARCHX86_MMU_COMMON_FLAG_PCD
#define ARCHX86_MMU_PDE_FLAG_A     ARCHX86_MMU_COMMON_FLAG_A
#define ARCHX86_MMU_PDE_FLAG_D     ARCHX86_MMU_COMMON_FLAG_D
#define ARCHX86_MMU_PDE_FLAG_PS    (1 << 7)
#define ARCHX86_MMU_PDE_FLAG_G     ARCHX86_MMU_COMMON_FLAG_G


#define ARCHX86_MMU_PTE_FLAG_P     ARCHX86_MMU_COMMON_FLAG_P
#define ARCHX86_MMU_PTE_FLAG_RW    ARCHX86_MMU_COMMON_FLAG_RW
#define ARCHX86_MMU_PTE_FLAG_US    ARCHX86_MMU_COMMON_FLAG_US
#define ARCHX86_MMU_PTE_FLAG_PWT   ARCHX86_MMU_COMMON_FLAG_PWT
#define ARCHX86_MMU_PTE_FLAG_PCD   ARCHX86_MMU_COMMON_FLAG_PCD
#define ARCHX86_MMU_PTE_FLAG_A     ARCHX86_MMU_COMMON_FLAG_A
#define ARCHX86_MMU_PTE_FLAG_D     ARCHX86_MMU_COMMON_FLAG_D
#define ARCHX86_MMU_PTE_FLAG_PAT   (1 << 7)
#define ARCHX86_MMU_PTE_FLAG_G     ARCHX86_MMU_COMMON_FLAG_G

#define ARCHX86_MMU_PAGE_SIZE        4096
#define ARCHX86_MMU_ENTRY_SIZE       4
#define ARCHX86_MMU_ENTRY_COUNT      1024

#define ARCHX86_MMU_KERNEL_PDE_START    768
#define ARCHX86_MMU_KERNEL_PDE_COUNT    (ARCHX86_MMU_ENTRY_COUNT - ARCHX86_MMU_KERNEL_PDE_START - 1)

#define ARCHX86_MMU_SCRATCH_PDE    (ARCHX86_MMU_KERNEL_PDE_START + ARCHX86_MMU_KERNEL_PDE_COUNT - 1)
#define ARCHX86_MMU_SCRATCH_PTE    (ARCHX86_MMU_ENTRY_COUNT - 1)

#define ARCHX86_MMU_MAX_MEMORY_PER_PTE  ARCHX86_MMU_PAGE_SIZE
#define ARCHX86_MMU_MAX_MEMORY_PER_PDE  (ARCHX86_MMU_MAX_MEMORY_PER_PTE * ARCHX86_MMU_ENTRY_COUNT)
#define ARCHX86_MMU_KERNEL_AREA_SIZE    (ARCHX86_MMU_MAX_MEMORY_PER_PDE * ARCHX86_MMU_KERNEL_PDE_COUNT)

// PDE for recursive mapping the PD itself
#define ARCHX86_MMU_PAGEDIR_PDE            (ARCHX86_MMU_ENTRY_COUNT - 1)

//------------------------------------------------------------------------------
// Below are only applicable to C
//------------------------------------------------------------------------------

#ifndef YJKERNEL_ASMFILE

#include <kernel/arch/mmu.h>
#include <kernel/lib/diagnostics.h>
#include <kernel/types.h>
#include <stdbool.h>
#include <stdint.h>

STATIC_ASSERT_TEST(sizeof(uint32_t) == ARCHX86_MMU_ENTRY_SIZE);
STATIC_ASSERT_TEST(ARCHX86_MMU_ENTRY_COUNT == (ARCHX86_MMU_PAGE_SIZE / ARCHX86_MMU_ENTRY_SIZE));

static uint8_t const ARCHX86_MMU_EMUTRANS_FAULT_FLAG_PDE_MISSING = 1 << 0;
static uint8_t const ARCHX86_MMU_EMUTRANS_FAULT_FLAG_PDE_WRITE   = 1 << 1;
static uint8_t const ARCHX86_MMU_EMUTRANS_FAULT_FLAG_PDE_USER    = 1 << 2;
static uint8_t const ARCHX86_MMU_EMUTRANS_FAULT_FLAG_PTE_MISSING = 1 << 3;
static uint8_t const ARCHX86_MMU_EMUTRANS_FAULT_FLAG_PTE_WRITE   = 1 << 4;
static uint8_t const ARCHX86_MMU_EMUTRANS_FAULT_FLAG_PTE_USER    = 1 << 5;

struct archx86_mmu_emulateresult {
    physptr physaddr;
    uint8_t faultflags; // See ARCHX86_MMU_EMUTRANS_*
};

void archx86_mmu_writeprotect_kerneltext(void);
void archx86_writeprotect_afterearlyinit(void);
void archx86_mmu_init(void);
void archx86_mmu_setupstackbottomtrap(void);

#endif
