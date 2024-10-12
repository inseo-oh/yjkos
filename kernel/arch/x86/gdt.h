#pragma once
#include <kernel/lib/diagnostics.h>
#include <stddef.h>
#include <stdint.h>

// XXX: Move this to `gdt.c`?

struct archx86_gdt_segmentdescriptor {
    uint16_t limit_b15tob0;
    uint16_t base_b15tob0;
    uint8_t base_b23tob16;
    uint8_t accessbyte;
    uint8_t limit_b19tob16_and_flags;
    uint8_t base_b31tob24;
};
STATIC_ASSERT_SIZE(struct archx86_gdt_segmentdescriptor, 8);

struct archx86_gdt {
    struct archx86_gdt_segmentdescriptor nulldescriptor;
    struct archx86_gdt_segmentdescriptor kernelcode;
    struct archx86_gdt_segmentdescriptor kerneldata;
    struct archx86_gdt_segmentdescriptor tss;
};
STATIC_ASSERT_SIZE(struct archx86_gdt, sizeof(struct archx86_gdt_segmentdescriptor) * 4);

enum {
    ARCHX86_GDT_KERNEL_CS = offsetof(struct archx86_gdt, kernelcode),
    ARCHX86_GDT_KERNEL_DS = offsetof(struct archx86_gdt, kerneldata),
    ARCHX86_GDT_TSS       = offsetof(struct archx86_gdt, tss),
};

void archx86_gdt_init(void);
void archx86_gdt_load(void);
void archx86_gdt_reloadselectors(void);

