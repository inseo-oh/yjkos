#include "gdt.h"
#include <kernel/lib/diagnostics.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct TSS TSS;
struct TSS {
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
STATIC_ASSERT_SIZE(TSS, 108);


static uint8_t const GDT_FLAG_G  = 1 << 3;
static uint8_t const GDT_FLAG_DB = 1 << 2;
static uint8_t const GDT_FLAG_L  = 1 << 1;

static uint8_t const GDT_ACCESS_FLAG_S       = 1 << 4; // Clear -> System segment descriptor
#define GDT_ACCESS_FLAG_DPL(_n)  (_n << 5)
static uint8_t const GDT_ACCESS_FLAG_DPL0    = GDT_ACCESS_FLAG_DPL(0);
static uint8_t const GDT_ACCESS_FLAG_DPL1    = GDT_ACCESS_FLAG_DPL(1);
static uint8_t const GDT_ACCESS_FLAG_DPL2    = GDT_ACCESS_FLAG_DPL(2);
static uint8_t const GDT_ACCESS_FLAG_DPL3    = GDT_ACCESS_FLAG_DPL(3);
#undef GDT_ACCESS_FLAG_DPL
static uint8_t const GDT_ACCESS_FLAG_P       = 1 << 7;

// Below applies to non-system segment descriptors
static uint8_t const GDT_ACCESS_FLAG_ACCESSED = 1 << 0;
static uint8_t const GDT_ACCESS_FLAG_RW       = 1 << 1; // Data segments: Writable bit, Code segments: Readable bit
static uint8_t const GDT_ACCESS_FLAG_DC       = 1 << 2;
static uint8_t const GDT_ACCESS_FLAG_E        = 1 << 3;

// Below applies to system segment descriptors
static uint8_t const GDT_ACCESS_FLAG_TYPE_LDT       = 0x2;
static uint8_t const GDT_ACCESS_FLAG_TYPE_TSS32_AVL = 0x9;
static uint8_t const GDT_ACCESS_FLAG_TYPE_BUSY      = 0xb;

static void initdescriptor(
    archx86_gdt_segmentdescriptor_t *out,
    uint32_t base,
    uint32_t limit,
    uint8_t flags,
    uint8_t access_byte
) {
    out->limit_b15tob0 = limit & 0xffff;
    out->base_b15tob0 = (base & 0xffff);
    out->base_b23tob16 = ((base >> 16) & 0xffff);
    out->accessbyte = access_byte;
    out->limit_b19tob16_and_flags = ((flags & 0xf) << 4) | ((limit >> 16) & 0xf);
    out->base_b31tob24 = ((base >> 24) & 0xff);
}

static archx86_gdt_t s_gdt;
static TSS s_tss;
static uint8_t s_esp0stack[4096];

void archx86_gdt_init(void) {
    // Setup TSS
    s_tss.ss0 = ARCHX86_GDT_KERNEL_DS;
    s_tss.esp0 = (uintptr_t)s_esp0stack;
    s_tss.iopb = sizeof(s_tss);

    // Setup gdt
    initdescriptor(
        &s_gdt.kernelcode, 0, 0xfffff,
        GDT_FLAG_G | GDT_FLAG_DB,
        GDT_ACCESS_FLAG_P | GDT_ACCESS_FLAG_S | GDT_ACCESS_FLAG_RW | GDT_ACCESS_FLAG_DPL0 | GDT_ACCESS_FLAG_E | GDT_ACCESS_FLAG_ACCESSED
    );
    initdescriptor(
        &s_gdt.kerneldata, 0, 0xfffff,
        GDT_FLAG_G | GDT_FLAG_DB,
        GDT_ACCESS_FLAG_P | GDT_ACCESS_FLAG_S | GDT_ACCESS_FLAG_RW | GDT_ACCESS_FLAG_DPL0 | GDT_ACCESS_FLAG_ACCESSED
    );
    initdescriptor(
        &s_gdt.tss, (uintptr_t)&s_tss, sizeof(s_tss) - 1,
        GDT_FLAG_DB, // TSS size is expressed as bytes, so we don't use G flag.
        GDT_ACCESS_FLAG_P | GDT_ACCESS_FLAG_DPL0 | GDT_ACCESS_FLAG_TYPE_TSS32_AVL
    );
}

void archx86_gdt_load(void) {
    typedef struct gdtr gdtr_t;
    struct gdtr {
        uint16_t size;
        uint32_t offset;
    } __attribute__((packed));

    volatile gdtr_t gdtr;
    gdtr.offset = (uintptr_t)&s_gdt;
    gdtr.size = sizeof(s_gdt);
    __asm__ volatile("lgdt (%0)" ::"r"(&gdtr));
}

void archx86_gdt_reloadselectors(void) {
    uint32_t cs = ARCHX86_GDT_KERNEL_CS;
    uint32_t ds = ARCHX86_GDT_KERNEL_DS;
    uint16_t tss = ARCHX86_GDT_TSS;

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
