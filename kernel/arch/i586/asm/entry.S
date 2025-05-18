#include "../mmu_ext.h"
#include "../thirdparty/multiboot.h"

#define MB_MAGIC      MULTIBOOT_HEADER_MAGIC
#define MB_FLAGS      (MULTIBOOT_PAGE_ALIGN | MULTIBOOT_MEMORY_INFO | MULTIBOOT_VIDEO_MODE)
#define MB_CHECKSUM   -(MB_MAGIC + MB_FLAGS)

/* Multiboot header */
.section .multiboot 
.align MULTIBOOT_INFO_ALIGN
.long MB_MAGIC
.long MB_FLAGS
.long MB_CHECKSUM
/* a.out kludge */
.skip 160 /* Skip it */
/* Video mode */
.long 0   /* mode_type - Linear graphics mode */
.long 0   /* width     - No preference */
.long 0   /* height    - No preference */
.long 0   /* depth     - No preference */

/******************************************************************************/
.section .text.init
.global archi586_kernelentry

#define PAGING_FLAGS   (ARCHI586_MMU_COMMON_FLAG_P | ARCHI586_MMU_COMMON_FLAG_RW)

archi586_kernelentry:
    /* Setup initial bootstrap stack */
    mov $earlystacktop, %esp

    /* Fill page table */
    /* -> Lower 2M directory contains code that we are running right now, so it must be 1:1 mapped */
    mov $0x00000000, %esi
    xor %edi, %edi
10:
    mov %esi, %ecx
    or $PAGING_FLAGS, %ecx
    mov %ecx, lower2mpagetable(,%edi,ARCHI586_MMU_ENTRY_SIZE)
    add $ARCHI586_MMU_PAGE_SIZE, %esi
    inc %edi
    cmp $ARCHI586_MMU_ENTRY_COUNT, %edi
    jne 10b
    /* -> Initialize kernel page table */
    mov $__physaddr_kernel_begin__, %esi
    xor %edi, %edi
10:
    /*
     * For now, we map everything since beginning of the kernel.
     * (Kernel will reconfigure memory mapping later)
     */
    mov %esi, %ecx
    or $PAGING_FLAGS, %ecx
    mov %ecx, kernelpagetable(,%edi,ARCHI586_MMU_ENTRY_SIZE)
    add $ARCHI586_MMU_PAGE_SIZE, %esi
    inc %edi
    cmp $(ARCHI586_MMU_KERNEL_PDE_COUNT * ARCHI586_MMU_ENTRY_COUNT), %edi
    jne 10b
    /* Initialize page directory */
    /* -> Lower 2M page table */
    mov $0, %edi
    mov $lower2mpagetable, %ecx
    or $PAGING_FLAGS, %ecx
    mov %ecx, pagedir(,%edi,ARCHI586_MMU_ENTRY_SIZE)
    /* -> Kernel page table */
    mov $ARCHI586_MMU_KERNEL_PDE_START, %edi
    mov $kernelpagetable, %ecx
    or $PAGING_FLAGS, %ecx
10:
    mov %ecx, pagedir(,%edi,ARCHI586_MMU_ENTRY_SIZE)
    add $ARCHI586_MMU_PAGE_SIZE, %ecx
    inc %edi
    cmp $ARCHI586_MMU_ENTRY_COUNT, %edi
    jne 10b
    /* -> Map directory table itself to one of PDEs */
    mov $ARCHI586_MMU_PAGEDIR_PDE, %edi
    mov $pagedir, %ecx
    or $PAGING_FLAGS, %ecx
    mov %ecx, pagedir(,%edi,ARCHI586_MMU_ENTRY_SIZE)

    /* Enable paging */
    mov $pagedir, %esi
    mov %esi, %cr3
    mov %cr0, %esi
    or $0x80010000, %esi /* Enable CR0.PG and CR0.WP */
    mov %esi, %cr0

    /* Switch to new stack */
    mov $stacktop, %esp

    /* Jump to main initialization code */
    mov $0, %ebp
    sub $8, %esp /* Subtract 8 bytes for 16-byte alignment */
    push %ebx
    push %eax
    call ArchI586_Init

	cli
1:	hlt
	jmp 1b

/******************************************************************************/
.section .bss.init
.align 16

earlystackbottom:
.skip 4096
earlystacktop:

.align 4096
pagedir:
.skip ARCHI586_MMU_PAGE_SIZE
pagedirend:

.align 4096
lower2mpagetable:
.skip ARCHI586_MMU_PAGE_SIZE
lower2mpagetableend:

.align 4096
kernelpagetable:
.skip (ARCHI586_MMU_KERNEL_PDE_COUNT * ARCHI586_MMU_PAGE_SIZE)
kernelpagetableend:

/******************************************************************************/

.section .bss

.align 4096
/*
 * A "trap" page that will be intentionally unmapped to immediately trigger page fault on stack overflow.
 * It won't prevent kernel from triple-fault, but at least it will do so with CR2 pointing to recongnizable
 * address, which will make debugging with emulators like QEMU a bit easier.
 */
.global archi586_stackbottomtrap
archi586_stackbottomtrap:
.skip 4096
archi586_stackbottomtrapend:

stackbottom:
.skip 16384
stacktop: