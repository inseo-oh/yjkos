#pragma once
#include <kernel/lib/diagnostics.h>
#include <stddef.h>
#include <stdint.h>

/* XXX: Move this to `gdt.c`? */

struct ArchI586_Gdt_SegmentDescriptor {
    uint16_t limit_b15tob0;
    uint16_t base_b15tob0;
    uint8_t base_b23tob16;
    uint8_t accessbyte;
    uint8_t limit_b19tob16_and_flags;
    uint8_t base_b31tob24;
};
STATIC_ASSERT_SIZE(struct ArchI586_Gdt_SegmentDescriptor, 8);

struct ArchI586_Gdt {
    struct ArchI586_Gdt_SegmentDescriptor nulldescriptor;
    struct ArchI586_Gdt_SegmentDescriptor kernelcode;
    struct ArchI586_Gdt_SegmentDescriptor kerneldata;
    struct ArchI586_Gdt_SegmentDescriptor tss;
};
STATIC_ASSERT_SIZE(struct ArchI586_Gdt, sizeof(struct ArchI586_Gdt_SegmentDescriptor) * 4);

#define ARCHI586_GDT_KERNEL_CS offsetof(struct ArchI586_Gdt, kernelcode)
#define ARCHI586_GDT_KERNEL_DS offsetof(struct ArchI586_Gdt, kerneldata)
#define ARCHI586_GDT_TSS offsetof(struct ArchI586_Gdt, tss)

void ArchI586_Gdt_Init(void);
void ArchI586_Gdt_Load(void);
void ArchI586_Gdt_ReloadSelectors(void);
