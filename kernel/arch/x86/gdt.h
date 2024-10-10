#pragma once
#include <kernel/lib/diagnostics.h>
#include <stddef.h>
#include <stdint.h>

typedef struct archx86_gdt_segmentdescriptor archx86_gdt_segmentdescriptor_t;
struct archx86_gdt_segmentdescriptor {
    uint16_t limit_b15tob0;
    uint16_t base_b15tob0;
    uint8_t base_b23tob16;
    uint8_t accessbyte;
    uint8_t limit_b19tob16_and_flags;
    uint8_t base_b31tob24;
};
STATIC_ASSERT_SIZE(struct archx86_gdt_segmentdescriptor, 8);

typedef struct archx86_gdt archx86_gdt_t;
struct archx86_gdt {
    archx86_gdt_segmentdescriptor_t nulldescriptor;
    archx86_gdt_segmentdescriptor_t kernelcode;
    archx86_gdt_segmentdescriptor_t kerneldata;
    archx86_gdt_segmentdescriptor_t tss;
};
STATIC_ASSERT_SIZE(archx86_gdt_t, sizeof(archx86_gdt_segmentdescriptor_t) * 4);

enum {
    ARCHX86_GDT_KERNEL_CS = offsetof(archx86_gdt_t, kernelcode),
    ARCHX86_GDT_KERNEL_DS = offsetof(archx86_gdt_t, kerneldata),
    ARCHX86_GDT_TSS       = offsetof(archx86_gdt_t, tss),
};

void archx86_gdt_init(void);
void archx86_gdt_load(void);
void archx86_gdt_reloadselectors(void);

