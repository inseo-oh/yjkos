#include "gdt.h"
#include <kernel/lib/diagnostics.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct tss {
    uint16_t link;
    uint16_t _reserved0;
    uint32_t esp0;
    uint16_t ss0;
    uint16_t _reserved1;
    uint32_t esp1;
    uint16_t ss1;
    uint16_t _reserved2;
    uint32_t esp2;
    uint16_t ss2;
    uint16_t _reserved3;
    uint32_t cr3;
    uint32_t eip;
    uint32_t eflags;
    uint32_t eax;
    uint32_t ecx;
    uint32_t edx;
    uint32_t ebx;
    uint32_t esp;
    uint32_t ebp;
    uint32_t esi;
    uint32_t edi;
    uint16_t es;
    uint16_t _reserved4;
    uint16_t cs;
    uint16_t _reserved5;
    uint16_t ss;
    uint16_t _reserved6;
    uint16_t ds;
    uint16_t _reserved7;
    uint16_t fs;
    uint16_t _reserved8;
    uint16_t gs;
    uint16_t _reserved9;
    uint16_t ldtr;
    uint16_t _reserved10;
    uint16_t _reserved11;
    uint16_t iopb;
    uint32_t ssp;
};
STATIC_ASSERT_SIZE(struct tss, 108);

#define GDT_FLAG_G                      (1U << 3)
#define GDT_FLAG_DB                     (1U << 2)
#define GDT_FLAG_L                      (1U << 1)
// Clear -> System segment descriptor
#define GDT_ACCESS_FLAG_S               (1U << 4)
#define GDT_ACCESS_FLAG_DPL(_n)         ((_n) << 5)
#define GDT_ACCESS_FLAG_DPL0            GDT_ACCESS_FLAG_DPL(0U)
#define GDT_ACCESS_FLAG_DPL1            GDT_ACCESS_FLAG_DPL(1U)
#define GDT_ACCESS_FLAG_DPL2            GDT_ACCESS_FLAG_DPL(2U)
#define GDT_ACCESS_FLAG_DPL3            GDT_ACCESS_FLAG_DPL(3U)
#define GDT_ACCESS_FLAG_P               (1U << 7)

// Below applies to non-system segment descriptors
#define GDT_ACCESS_FLAG_ACCESSED        (1U << 0)
// Data segments: Writable bit, Code segments: Readable bit
#define GDT_ACCESS_FLAG_RW              (1U << 1)
#define GDT_ACCESS_FLAG_DC              (1U << 2)
#define GDT_ACCESS_FLAG_E               (1U << 3)

// Below applies to system segment descriptors
#define GDT_ACCESS_FLAG_TYPE_LDT        0x2U
#define GDT_ACCESS_FLAG_TYPE_TSS32_AVL  0x9U
#define GDT_ACCESS_FLAG_TYPE_BUSY       0xbU

static void init_descriptor(
    struct archi586_gdt_segment_descriptor *out,
    uint32_t base,
    uint32_t limit,
    uint8_t flags,
    uint8_t access_byte
) {
    out->limit_b15tob0 = limit & 0xffffU;
    out->base_b15tob0 = (base & 0xffffU);
    out->base_b23tob16 = ((base >> 16) & 0xffffU);
    out->accessbyte = access_byte;
    out->limit_b19tob16_and_flags =
        ((flags & 0xfU) << 4) | ((limit >> 16) & 0xfU);
    out->base_b31tob24 = ((base >> 24) & 0xffU);
}

static struct archi586_gdt s_gdt;
static struct tss s_tss;
static uint8_t s_esp0stack[4096];

void archi586_gdt_init(void) {
    // Setup TSS
    s_tss.ss0 = ARCHI586_GDT_KERNEL_DS;
    s_tss.esp0 = (uintptr_t)s_esp0stack;
    s_tss.iopb = sizeof(s_tss);

    // Setup gdt
    init_descriptor(
        &s_gdt.kernelcode, 0, 0xfffff,
        GDT_FLAG_G | GDT_FLAG_DB,
        GDT_ACCESS_FLAG_P | GDT_ACCESS_FLAG_S | GDT_ACCESS_FLAG_RW | GDT_ACCESS_FLAG_DPL0 | GDT_ACCESS_FLAG_E | GDT_ACCESS_FLAG_ACCESSED
    );
    init_descriptor(
        &s_gdt.kerneldata, 0, 0xfffff,
        GDT_FLAG_G | GDT_FLAG_DB,
        GDT_ACCESS_FLAG_P | GDT_ACCESS_FLAG_S | GDT_ACCESS_FLAG_RW | GDT_ACCESS_FLAG_DPL0 | GDT_ACCESS_FLAG_ACCESSED
    );
    init_descriptor(
        &s_gdt.tss, (uintptr_t)&s_tss, sizeof(s_tss) - 1,
        GDT_FLAG_DB, // TSS size is expressed as bytes, so we don't use G flag.
        GDT_ACCESS_FLAG_P | GDT_ACCESS_FLAG_DPL0 | GDT_ACCESS_FLAG_TYPE_TSS32_AVL
    );
}

void archi586_gdt_load(void) {
    struct gdtr {
        uint16_t size;
        uint32_t offset;
    } __attribute__((packed));

    volatile struct gdtr gdtr;
    gdtr.offset = (uintptr_t)&s_gdt;
    gdtr.size = sizeof(s_gdt);
    __asm__ volatile("lgdt (%0)" ::"r"(&gdtr));
}

void archi586_gdt_reloadselectors(void) {
    uint32_t cs = ARCHI586_GDT_KERNEL_CS;
    uint32_t ds = ARCHI586_GDT_KERNEL_DS;
    uint16_t tss = ARCHI586_GDT_TSS;

    // Reload segment selectors
    __asm__ volatile(
        "  lea 1f, %%eax\n"
        "  push %0\n"
        "  push %%eax\n"
        "  retf\n"
        "1:\n"
        "  mov %1, %%ds\n"
        "  mov %1, %%es\n"
        "  mov %1, %%fs\n"
        "  mov %1, %%gs\n"
        "  mov %1, %%ss\n"
        "  ltr %2\n"
        :: "r"(cs),
           "r"(ds),
           "r"(tss)
        : "eax" // Used as temporary storage for LEA result
    );
}
