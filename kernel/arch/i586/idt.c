#include "idt.h"
#include "asm/interruptentry.h"
#include "gdt.h"
#include <kernel/io/co.h>
#include <kernel/lib/diagnostics.h>
#include <kernel/lib/strutil.h>
#include <stdint.h>

struct gate_descriptor {
    uint16_t offset_b15tob0;
    uint16_t segmentselector;
    uint8_t _reserved0;
    uint8_t flags;
    uint16_t offset_b31tob16;
};
STATIC_ASSERT_SIZE(struct gate_descriptor, 8);

#define IDT_FLAG_TYPE_INT32 (0xeU << 0)
#define IDT_FLAG_TYPE_TRAP32 (0xfU << 0)
#define IDT_FLAG_DPL(_n) ((_n) << 5)
#define IDT_FLAG_DPL0 IDT_FLAG_DPL(0U)
#define IDT_FLAG_DPL1 IDT_FLAG_DPL(1U)
#define IDT_FLAG_DPL2 IDT_FLAG_DPL(2U)
#define IDT_FLAG_DPL3 IDT_FLAG_DPL(3U)
#define IDT_FLAG_P (1U << 7)

struct idt {
    struct gate_descriptor entries[256];
};

static void init_descriptor(struct gate_descriptor *out, uint32_t offset, uint16_t flags) {
    memset(out, 0, sizeof(*out));
    out->offset_b15tob0 = offset;
    out->segmentselector = ARCHI586_GDT_KERNEL_CS;
    out->flags = flags;
    out->offset_b31tob16 = offset >> 16;
}

typedef void(HANDLER)(void);

static HANDLER *KERNEL_TRAPS[32] = {
    archi586_isr_exception0_entry,
    archi586_isr_exception1_entry,
    archi586_isr_exception2_entry,
    archi586_isr_exception3_entry,
    archi586_isr_exception4_entry,
    archi586_isr_exception5_entry,
    archi586_isr_exception6_entry,
    archi586_isr_exception7_entry,
    archi586_isr_exception8_entry,
    archi586_isr_exception9_entry,
    archi586_isr_exception10_entry,
    archi586_isr_exception11_entry,
    archi586_isr_exception12_entry,
    archi586_isr_exception13_entry,
    archi586_isr_exception14_entry,
    archi586_isr_exception15_entry,
    archi586_isr_exception16_entry,
    archi586_isr_exception17_entry,
    archi586_isr_exception18_entry,
    archi586_isr_exception19_entry,
    archi586_isr_exception20_entry,
    archi586_isr_exception21_entry,
    archi586_isr_exception22_entry,
    archi586_isr_exception23_entry,
    archi586_isr_exception24_entry,
    archi586_isr_exception25_entry,
    archi586_isr_exception26_entry,
    archi586_isr_exception27_entry,
    archi586_isr_exception28_entry,
    archi586_isr_exception29_entry,
    archi586_isr_exception30_entry,
    archi586_isr_exception31_entry,
};

static HANDLER *KERNEL_INTERRUPT_HANDLERS[] = {
    archi586_isr_interrupt32_entry,
    archi586_isr_interrupt33_entry,
    archi586_isr_interrupt34_entry,
    archi586_isr_interrupt35_entry,
    archi586_isr_interrupt36_entry,
    archi586_isr_interrupt37_entry,
    archi586_isr_interrupt38_entry,
    archi586_isr_interrupt39_entry,
    archi586_isr_interrupt40_entry,
    archi586_isr_interrupt41_entry,
    archi586_isr_interrupt42_entry,
    archi586_isr_interrupt43_entry,
    archi586_isr_interrupt44_entry,
    archi586_isr_interrupt45_entry,
    archi586_isr_interrupt46_entry,
    archi586_isr_interrupt47_entry,
    archi586_isr_interrupt48_entry,
    archi586_isr_interrupt49_entry,
    archi586_isr_interrupt50_entry,
    archi586_isr_interrupt51_entry,
    archi586_isr_interrupt52_entry,
    archi586_isr_interrupt53_entry,
    archi586_isr_interrupt54_entry,
    archi586_isr_interrupt55_entry,
    archi586_isr_interrupt56_entry,
    archi586_isr_interrupt57_entry,
    archi586_isr_interrupt58_entry,
    archi586_isr_interrupt59_entry,
    archi586_isr_interrupt60_entry,
    archi586_isr_interrupt61_entry,
    archi586_isr_interrupt62_entry,
    archi586_isr_interrupt63_entry,
    archi586_isr_interrupt64_entry,
    archi586_isr_interrupt65_entry,
    archi586_isr_interrupt66_entry,
    archi586_isr_interrupt67_entry,
    archi586_isr_interrupt68_entry,
    archi586_isr_interrupt69_entry,
    archi586_isr_interrupt70_entry,
    archi586_isr_interrupt71_entry,
    archi586_isr_interrupt72_entry,
    archi586_isr_interrupt73_entry,
    archi586_isr_interrupt74_entry,
    archi586_isr_interrupt75_entry,
    archi586_isr_interrupt76_entry,
    archi586_isr_interrupt77_entry,
    archi586_isr_interrupt78_entry,
    archi586_isr_interrupt79_entry,
    archi586_isr_interrupt80_entry,
    archi586_isr_interrupt81_entry,
    archi586_isr_interrupt82_entry,
    archi586_isr_interrupt83_entry,
    archi586_isr_interrupt84_entry,
    archi586_isr_interrupt85_entry,
    archi586_isr_interrupt86_entry,
    archi586_isr_interrupt87_entry,
    archi586_isr_interrupt88_entry,
    archi586_isr_interrupt89_entry,
    archi586_isr_interrupt90_entry,
    archi586_isr_interrupt91_entry,
    archi586_isr_interrupt92_entry,
    archi586_isr_interrupt93_entry,
    archi586_isr_interrupt94_entry,
    archi586_isr_interrupt95_entry,
    archi586_isr_interrupt96_entry,
    archi586_isr_interrupt97_entry,
    archi586_isr_interrupt98_entry,
    archi586_isr_interrupt99_entry,
    archi586_isr_interrupt100_entry,
    archi586_isr_interrupt101_entry,
    archi586_isr_interrupt102_entry,
    archi586_isr_interrupt103_entry,
    archi586_isr_interrupt104_entry,
    archi586_isr_interrupt105_entry,
    archi586_isr_interrupt106_entry,
    archi586_isr_interrupt107_entry,
    archi586_isr_interrupt108_entry,
    archi586_isr_interrupt109_entry,
    archi586_isr_interrupt110_entry,
    archi586_isr_interrupt111_entry,
    archi586_isr_interrupt112_entry,
    archi586_isr_interrupt113_entry,
    archi586_isr_interrupt114_entry,
    archi586_isr_interrupt115_entry,
    archi586_isr_interrupt116_entry,
    archi586_isr_interrupt117_entry,
    archi586_isr_interrupt118_entry,
    archi586_isr_interrupt119_entry,
    archi586_isr_interrupt120_entry,
    archi586_isr_interrupt121_entry,
    archi586_isr_interrupt122_entry,
    archi586_isr_interrupt123_entry,
    archi586_isr_interrupt124_entry,
    archi586_isr_interrupt125_entry,
    archi586_isr_interrupt126_entry,
    archi586_isr_interrupt127_entry,
    archi586_isr_interrupt128_entry,
    archi586_isr_interrupt129_entry,
    archi586_isr_interrupt130_entry,
    archi586_isr_interrupt131_entry,
    archi586_isr_interrupt132_entry,
    archi586_isr_interrupt133_entry,
    archi586_isr_interrupt134_entry,
    archi586_isr_interrupt135_entry,
    archi586_isr_interrupt136_entry,
    archi586_isr_interrupt137_entry,
    archi586_isr_interrupt138_entry,
    archi586_isr_interrupt139_entry,
    archi586_isr_interrupt140_entry,
    archi586_isr_interrupt141_entry,
    archi586_isr_interrupt142_entry,
    archi586_isr_interrupt143_entry,
    archi586_isr_interrupt144_entry,
    archi586_isr_interrupt145_entry,
    archi586_isr_interrupt146_entry,
    archi586_isr_interrupt147_entry,
    archi586_isr_interrupt148_entry,
    archi586_isr_interrupt149_entry,
    archi586_isr_interrupt150_entry,
    archi586_isr_interrupt151_entry,
    archi586_isr_interrupt152_entry,
    archi586_isr_interrupt153_entry,
    archi586_isr_interrupt154_entry,
    archi586_isr_interrupt155_entry,
    archi586_isr_interrupt156_entry,
    archi586_isr_interrupt157_entry,
    archi586_isr_interrupt158_entry,
    archi586_isr_interrupt159_entry,
    archi586_isr_interrupt160_entry,
    archi586_isr_interrupt161_entry,
    archi586_isr_interrupt162_entry,
    archi586_isr_interrupt163_entry,
    archi586_isr_interrupt164_entry,
    archi586_isr_interrupt165_entry,
    archi586_isr_interrupt166_entry,
    archi586_isr_interrupt167_entry,
    archi586_isr_interrupt168_entry,
    archi586_isr_interrupt169_entry,
    archi586_isr_interrupt170_entry,
    archi586_isr_interrupt171_entry,
    archi586_isr_interrupt172_entry,
    archi586_isr_interrupt173_entry,
    archi586_isr_interrupt174_entry,
    archi586_isr_interrupt175_entry,
    archi586_isr_interrupt176_entry,
    archi586_isr_interrupt177_entry,
    archi586_isr_interrupt178_entry,
    archi586_isr_interrupt179_entry,
    archi586_isr_interrupt180_entry,
    archi586_isr_interrupt181_entry,
    archi586_isr_interrupt182_entry,
    archi586_isr_interrupt183_entry,
    archi586_isr_interrupt184_entry,
    archi586_isr_interrupt185_entry,
    archi586_isr_interrupt186_entry,
    archi586_isr_interrupt187_entry,
    archi586_isr_interrupt188_entry,
    archi586_isr_interrupt189_entry,
    archi586_isr_interrupt190_entry,
    archi586_isr_interrupt191_entry,
    archi586_isr_interrupt192_entry,
    archi586_isr_interrupt193_entry,
    archi586_isr_interrupt194_entry,
    archi586_isr_interrupt195_entry,
    archi586_isr_interrupt196_entry,
    archi586_isr_interrupt197_entry,
    archi586_isr_interrupt198_entry,
    archi586_isr_interrupt199_entry,
    archi586_isr_interrupt200_entry,
    archi586_isr_interrupt201_entry,
    archi586_isr_interrupt202_entry,
    archi586_isr_interrupt203_entry,
    archi586_isr_interrupt204_entry,
    archi586_isr_interrupt205_entry,
    archi586_isr_interrupt206_entry,
    archi586_isr_interrupt207_entry,
    archi586_isr_interrupt208_entry,
    archi586_isr_interrupt209_entry,
    archi586_isr_interrupt210_entry,
    archi586_isr_interrupt211_entry,
    archi586_isr_interrupt212_entry,
    archi586_isr_interrupt213_entry,
    archi586_isr_interrupt214_entry,
    archi586_isr_interrupt215_entry,
    archi586_isr_interrupt216_entry,
    archi586_isr_interrupt217_entry,
    archi586_isr_interrupt218_entry,
    archi586_isr_interrupt219_entry,
    archi586_isr_interrupt220_entry,
    archi586_isr_interrupt221_entry,
    archi586_isr_interrupt222_entry,
    archi586_isr_interrupt223_entry,
    archi586_isr_interrupt224_entry,
    archi586_isr_interrupt225_entry,
    archi586_isr_interrupt226_entry,
    archi586_isr_interrupt227_entry,
    archi586_isr_interrupt228_entry,
    archi586_isr_interrupt229_entry,
    archi586_isr_interrupt230_entry,
    archi586_isr_interrupt231_entry,
    archi586_isr_interrupt232_entry,
    archi586_isr_interrupt233_entry,
    archi586_isr_interrupt234_entry,
    archi586_isr_interrupt235_entry,
    archi586_isr_interrupt236_entry,
    archi586_isr_interrupt237_entry,
    archi586_isr_interrupt238_entry,
    archi586_isr_interrupt239_entry,
    archi586_isr_interrupt240_entry,
    archi586_isr_interrupt241_entry,
    archi586_isr_interrupt242_entry,
    archi586_isr_interrupt243_entry,
    archi586_isr_interrupt244_entry,
    archi586_isr_interrupt245_entry,
    archi586_isr_interrupt246_entry,
    archi586_isr_interrupt247_entry,
    archi586_isr_interrupt248_entry,
    archi586_isr_interrupt249_entry,
    archi586_isr_interrupt250_entry,
    archi586_isr_interrupt251_entry,
    archi586_isr_interrupt252_entry,
    archi586_isr_interrupt253_entry,
    archi586_isr_interrupt254_entry,
    archi586_isr_interrupt255_entry,
};

static struct idt s_idt [[gnu::section(".data.ro_after_early_init")]];

void archi586_idt_init(void) {
    enum {
        KERNEL_TRAP_COUNT = sizeof(KERNEL_TRAPS) / sizeof(*KERNEL_TRAPS),
        KERNEL_INT_HANDLER_COUNT = sizeof(KERNEL_INTERRUPT_HANDLERS) / sizeof(*KERNEL_INTERRUPT_HANDLERS),
        TOTAL_HANDLER_COUNT = sizeof(s_idt.entries) / sizeof(*s_idt.entries),
    };
    static_assert(KERNEL_TRAP_COUNT + KERNEL_INT_HANDLER_COUNT == TOTAL_HANDLER_COUNT, "unhandled interrupts exist");

    for (size_t i = 0; i < KERNEL_TRAP_COUNT; ++i) {
        init_descriptor(&s_idt.entries[i], (uintptr_t)KERNEL_TRAPS[i], IDT_FLAG_P | IDT_FLAG_TYPE_TRAP32 | IDT_FLAG_DPL0);
    }
    for (size_t i = 0; i < KERNEL_INT_HANDLER_COUNT; ++i) {
        init_descriptor(&s_idt.entries[i + KERNEL_TRAP_COUNT], (uintptr_t)KERNEL_INTERRUPT_HANDLERS[i], IDT_FLAG_P | IDT_FLAG_TYPE_INT32 | IDT_FLAG_DPL0);
    }
}

void archi586_idt_load(void) {
    struct [[gnu::packed]] idtr {
        uint16_t size;
        uint32_t offset;
    };

    volatile struct idtr idtr;
    idtr.offset = (uintptr_t)&s_idt;
    idtr.size = sizeof(s_idt);
    __asm__ volatile("lidt (%0)" ::"r"(&idtr));
}

void archi586_idt_test(void) {
    co_printf("triggering divide by zero for testing\n");
    __asm__ volatile(
        "mov $0, %eax\n"
        "mov $0x11111111, %edi\n"
        "mov $0x22222222, %esi\n"
        "mov $0x44444444, %ebx\n"
        "mov $0x55555555, %edx\n"
        "mov $0x66666666, %ecx\n"
        "idiv %eax\n");
}
