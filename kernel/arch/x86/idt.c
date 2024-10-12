#include "asm/interruptentry.h"
#include "gdt.h"
#include "idt.h"
#include <kernel/io/tty.h>
#include <kernel/lib/diagnostics.h>
#include <stdint.h>
#include <string.h>

struct gatedescriptor {
    uint16_t offset_b15tob0;
    uint16_t segmentselector;
    uint8_t _reserved0;
    uint8_t flags;
    uint16_t offset_b31tob16;
};
STATIC_ASSERT_SIZE(struct gatedescriptor, 8);

static uint8_t const IDT_FLAG_TYPE_INT32  = 0xe << 0; 
static uint8_t const IDT_FLAG_TYPE_TRAP32 = 0xf << 0; 
#define IDT_FLAG_DPL(_n)     ((_n) << 5)
static uint8_t const IDT_FLAG_DPL0 = IDT_FLAG_DPL(0);
static uint8_t const IDT_FLAG_DPL1 = IDT_FLAG_DPL(1);
static uint8_t const IDT_FLAG_DPL2 = IDT_FLAG_DPL(2);
static uint8_t const IDT_FLAG_DPL3 = IDT_FLAG_DPL(3);
#undef IDT_FLAG_DPL
static uint8_t const IDT_FLAG_P = 1 << 7;

struct idt {
    struct gatedescriptor entries[256];
};

static void initdescriptor(struct gatedescriptor *out, uint32_t offset, uint16_t flags) {
    memset(out, 0, sizeof(*out));
    out->offset_b15tob0 = offset;
    out->segmentselector = ARCHX86_GDT_KERNEL_CS;
    out->flags = flags;
    out->offset_b31tob16 = offset >> 16;
}

typedef void(handler)(void);

static handler *KERNEL_TRAPS[32] = {
    archx86_isr_exception0entry,  archx86_isr_exception1entry,  archx86_isr_exception2entry,  archx86_isr_exception3entry,
    archx86_isr_exception4entry,  archx86_isr_exception5entry,  archx86_isr_exception6entry,  archx86_isr_exception7entry,
    archx86_isr_exception8entry,  archx86_isr_exception9entry,  archx86_isr_exception10entry, archx86_isr_exception11entry,
    archx86_isr_exception12entry, archx86_isr_exception13entry, archx86_isr_exception14entry, archx86_isr_exception15entry,
    archx86_isr_exception16entry, archx86_isr_exception17entry, archx86_isr_exception18entry, archx86_isr_exception19entry,
    archx86_isr_exception20entry, archx86_isr_exception21entry, archx86_isr_exception22entry, archx86_isr_exception23entry,
    archx86_isr_exception24entry, archx86_isr_exception25entry, archx86_isr_exception26entry, archx86_isr_exception27entry,
    archx86_isr_exception28entry, archx86_isr_exception29entry, archx86_isr_exception30entry, archx86_isr_exception31entry,
};

static handler *KERNEL_INTERRUPT_HANDLERS[] = {
    archx86_isr_interrupt32entry,  archx86_isr_interrupt33entry,  archx86_isr_interrupt34entry,  archx86_isr_interrupt35entry,
    archx86_isr_interrupt36entry,  archx86_isr_interrupt37entry,  archx86_isr_interrupt38entry,  archx86_isr_interrupt39entry,
    archx86_isr_interrupt40entry,  archx86_isr_interrupt41entry,  archx86_isr_interrupt42entry,  archx86_isr_interrupt43entry,
    archx86_isr_interrupt44entry,  archx86_isr_interrupt45entry,  archx86_isr_interrupt46entry,  archx86_isr_interrupt47entry,
    archx86_isr_interrupt48entry,  archx86_isr_interrupt49entry,  archx86_isr_interrupt50entry,  archx86_isr_interrupt51entry,
    archx86_isr_interrupt52entry,  archx86_isr_interrupt53entry,  archx86_isr_interrupt54entry,  archx86_isr_interrupt55entry,
    archx86_isr_interrupt56entry,  archx86_isr_interrupt57entry,  archx86_isr_interrupt58entry,  archx86_isr_interrupt59entry,
    archx86_isr_interrupt60entry,  archx86_isr_interrupt61entry,  archx86_isr_interrupt62entry,  archx86_isr_interrupt63entry,
    archx86_isr_interrupt64entry,  archx86_isr_interrupt65entry,  archx86_isr_interrupt66entry,  archx86_isr_interrupt67entry,
    archx86_isr_interrupt68entry,  archx86_isr_interrupt69entry,  archx86_isr_interrupt70entry,  archx86_isr_interrupt71entry,
    archx86_isr_interrupt72entry,  archx86_isr_interrupt73entry,  archx86_isr_interrupt74entry,  archx86_isr_interrupt75entry,
    archx86_isr_interrupt76entry,  archx86_isr_interrupt77entry,  archx86_isr_interrupt78entry,  archx86_isr_interrupt79entry,
    archx86_isr_interrupt80entry,  archx86_isr_interrupt81entry,  archx86_isr_interrupt82entry,  archx86_isr_interrupt83entry,
    archx86_isr_interrupt84entry,  archx86_isr_interrupt85entry,  archx86_isr_interrupt86entry,  archx86_isr_interrupt87entry,
    archx86_isr_interrupt88entry,  archx86_isr_interrupt89entry,  archx86_isr_interrupt90entry,  archx86_isr_interrupt91entry,
    archx86_isr_interrupt92entry,  archx86_isr_interrupt93entry,  archx86_isr_interrupt94entry,  archx86_isr_interrupt95entry,
    archx86_isr_interrupt96entry,  archx86_isr_interrupt97entry,  archx86_isr_interrupt98entry,  archx86_isr_interrupt99entry,
    archx86_isr_interrupt100entry, archx86_isr_interrupt101entry, archx86_isr_interrupt102entry, archx86_isr_interrupt103entry,
    archx86_isr_interrupt104entry, archx86_isr_interrupt105entry, archx86_isr_interrupt106entry, archx86_isr_interrupt107entry,
    archx86_isr_interrupt108entry, archx86_isr_interrupt109entry, archx86_isr_interrupt110entry, archx86_isr_interrupt111entry,
    archx86_isr_interrupt112entry, archx86_isr_interrupt113entry, archx86_isr_interrupt114entry, archx86_isr_interrupt115entry,
    archx86_isr_interrupt116entry, archx86_isr_interrupt117entry, archx86_isr_interrupt118entry, archx86_isr_interrupt119entry,
    archx86_isr_interrupt120entry, archx86_isr_interrupt121entry, archx86_isr_interrupt122entry, archx86_isr_interrupt123entry,
    archx86_isr_interrupt124entry, archx86_isr_interrupt125entry, archx86_isr_interrupt126entry, archx86_isr_interrupt127entry,
    archx86_isr_interrupt128entry, archx86_isr_interrupt129entry, archx86_isr_interrupt130entry, archx86_isr_interrupt131entry,
    archx86_isr_interrupt132entry, archx86_isr_interrupt133entry, archx86_isr_interrupt134entry, archx86_isr_interrupt135entry,
    archx86_isr_interrupt136entry, archx86_isr_interrupt137entry, archx86_isr_interrupt138entry, archx86_isr_interrupt139entry,
    archx86_isr_interrupt140entry, archx86_isr_interrupt141entry, archx86_isr_interrupt142entry, archx86_isr_interrupt143entry,
    archx86_isr_interrupt144entry, archx86_isr_interrupt145entry, archx86_isr_interrupt146entry, archx86_isr_interrupt147entry,
    archx86_isr_interrupt148entry, archx86_isr_interrupt149entry, archx86_isr_interrupt150entry, archx86_isr_interrupt151entry,
    archx86_isr_interrupt152entry, archx86_isr_interrupt153entry, archx86_isr_interrupt154entry, archx86_isr_interrupt155entry,
    archx86_isr_interrupt156entry, archx86_isr_interrupt157entry, archx86_isr_interrupt158entry, archx86_isr_interrupt159entry,
    archx86_isr_interrupt160entry, archx86_isr_interrupt161entry, archx86_isr_interrupt162entry, archx86_isr_interrupt163entry,
    archx86_isr_interrupt164entry, archx86_isr_interrupt165entry, archx86_isr_interrupt166entry, archx86_isr_interrupt167entry,
    archx86_isr_interrupt168entry, archx86_isr_interrupt169entry, archx86_isr_interrupt170entry, archx86_isr_interrupt171entry,
    archx86_isr_interrupt172entry, archx86_isr_interrupt173entry, archx86_isr_interrupt174entry, archx86_isr_interrupt175entry,
    archx86_isr_interrupt176entry, archx86_isr_interrupt177entry, archx86_isr_interrupt178entry, archx86_isr_interrupt179entry,
    archx86_isr_interrupt180entry, archx86_isr_interrupt181entry, archx86_isr_interrupt182entry, archx86_isr_interrupt183entry,
    archx86_isr_interrupt184entry, archx86_isr_interrupt185entry, archx86_isr_interrupt186entry, archx86_isr_interrupt187entry,
    archx86_isr_interrupt188entry, archx86_isr_interrupt189entry, archx86_isr_interrupt190entry, archx86_isr_interrupt191entry,
    archx86_isr_interrupt192entry, archx86_isr_interrupt193entry, archx86_isr_interrupt194entry, archx86_isr_interrupt195entry,
    archx86_isr_interrupt196entry, archx86_isr_interrupt197entry, archx86_isr_interrupt198entry, archx86_isr_interrupt199entry,
    archx86_isr_interrupt200entry, archx86_isr_interrupt201entry, archx86_isr_interrupt202entry, archx86_isr_interrupt203entry,
    archx86_isr_interrupt204entry, archx86_isr_interrupt205entry, archx86_isr_interrupt206entry, archx86_isr_interrupt207entry,
    archx86_isr_interrupt208entry, archx86_isr_interrupt209entry, archx86_isr_interrupt210entry, archx86_isr_interrupt211entry,
    archx86_isr_interrupt212entry, archx86_isr_interrupt213entry, archx86_isr_interrupt214entry, archx86_isr_interrupt215entry,
    archx86_isr_interrupt216entry, archx86_isr_interrupt217entry, archx86_isr_interrupt218entry, archx86_isr_interrupt219entry,
    archx86_isr_interrupt220entry, archx86_isr_interrupt221entry, archx86_isr_interrupt222entry, archx86_isr_interrupt223entry,
    archx86_isr_interrupt224entry, archx86_isr_interrupt225entry, archx86_isr_interrupt226entry, archx86_isr_interrupt227entry,
    archx86_isr_interrupt228entry, archx86_isr_interrupt229entry, archx86_isr_interrupt230entry, archx86_isr_interrupt231entry,
    archx86_isr_interrupt232entry, archx86_isr_interrupt233entry, archx86_isr_interrupt234entry, archx86_isr_interrupt235entry,
    archx86_isr_interrupt236entry, archx86_isr_interrupt237entry, archx86_isr_interrupt238entry, archx86_isr_interrupt239entry,
    archx86_isr_interrupt240entry, archx86_isr_interrupt241entry, archx86_isr_interrupt242entry, archx86_isr_interrupt243entry,
    archx86_isr_interrupt244entry, archx86_isr_interrupt245entry, archx86_isr_interrupt246entry, archx86_isr_interrupt247entry,
    archx86_isr_interrupt248entry, archx86_isr_interrupt249entry, archx86_isr_interrupt250entry, archx86_isr_interrupt251entry,
    archx86_isr_interrupt252entry, archx86_isr_interrupt253entry, archx86_isr_interrupt254entry, archx86_isr_interrupt255entry,
};

static struct idt s_idt __attribute__((section(".data.ro_after_early_init")));

void archx86_idt_init(void) {
    enum {
        KERNEL_TRAP_COUNT        = sizeof(KERNEL_TRAPS) / sizeof(*KERNEL_TRAPS),
        KERNEL_INT_HANDLER_COUNT = sizeof(KERNEL_INTERRUPT_HANDLERS) / sizeof(*KERNEL_INTERRUPT_HANDLERS),
        TOTAL_HANDLER_COUNT      = sizeof(s_idt.entries)/sizeof(*s_idt.entries),
    };
    static_assert(KERNEL_TRAP_COUNT + KERNEL_INT_HANDLER_COUNT == TOTAL_HANDLER_COUNT, "unhandled interrupts exist");

    for (size_t i = 0; i < KERNEL_TRAP_COUNT; ++i) {
        initdescriptor(
            &s_idt.entries[i], (uintptr_t)KERNEL_TRAPS[i],
            IDT_FLAG_P | IDT_FLAG_TYPE_TRAP32 | IDT_FLAG_DPL0
        ); 
    }
    for (size_t i = 0; i < KERNEL_INT_HANDLER_COUNT; ++i) {
        initdescriptor(
            &s_idt.entries[i + KERNEL_TRAP_COUNT], (uintptr_t)KERNEL_INTERRUPT_HANDLERS[i],
            IDT_FLAG_P | IDT_FLAG_TYPE_INT32 | IDT_FLAG_DPL0
        ); 
    }
}

void archx86_idt_load(void) {
    struct idtr {
        uint16_t size;
        uint32_t offset;
    } __attribute__((packed));

    volatile struct idtr idtr;
    idtr.offset = (uintptr_t)&s_idt;
    idtr.size = sizeof(s_idt);
    __asm__ volatile("lidt (%0)" ::"r"(&idtr));
}

void archx86_idt_test(void) {
    tty_printf("triggering divide by zero for testing\n");
    __asm__ volatile(
        "mov $0, %eax\n"
        "mov $0x11111111, %edi\n"
        "mov $0x22222222, %esi\n"
        "mov $0x44444444, %ebx\n"
        "mov $0x55555555, %edx\n"
        "mov $0x66666666, %ecx\n"
        "idiv %eax\n"
    );
}
