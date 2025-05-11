#pragma once
#include <kernel/lib/diagnostics.h>
#include <stddef.h>
#include <stdint.h>

// XXX: Move this to `gdt.c`?

struct archi586_gdt_segment_descriptor {
    uint16_t limit_b15tob0;
    uint16_t base_b15tob0;
    uint8_t base_b23tob16;
    uint8_t accessbyte;
    uint8_t limit_b19tob16_and_flags;
    uint8_t base_b31tob24;
};
STATIC_ASSERT_SIZE(struct archi586_gdt_segment_descriptor, 8);

struct archi586_gdt {
    struct archi586_gdt_segment_descriptor nulldescriptor;
    struct archi586_gdt_segment_descriptor kernelcode;
    struct archi586_gdt_segment_descriptor kerneldata;
    struct archi586_gdt_segment_descriptor tss;
};
STATIC_ASSERT_SIZE(struct archi586_gdt, sizeof(struct archi586_gdt_segment_descriptor) * 4);

#define ARCHI586_GDT_KERNEL_CS offsetof(struct archi586_gdt, kernelcode)
#define ARCHI586_GDT_KERNEL_DS offsetof(struct archi586_gdt, kerneldata)
#define ARCHI586_GDT_TSS offsetof(struct archi586_gdt, tss)

void archi586_gdt_init(void);
void archi586_gdt_load(void);
void archi586_gdt_reload_selectors(void);
