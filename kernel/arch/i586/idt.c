#include "asm/interruptentry.h"
#include "gdt.h"
#include "idt.h"
#include <kernel/io/co.h>
#include <kernel/lib/diagnostics.h>
#include <stdint.h>
#include <string.h>

struct gate_descriptor {
    uint16_t offset_b15tob0;
    uint16_t segmentselector;
    uint8_t _reserved0;
    uint8_t flags;
    uint16_t offset_b31tob16;
};
STATIC_ASSERT_SIZE(struct gate_descriptor, 8);

#define IDT_FLAG_TYPE_INT32     (0xeU << 0)
#define IDT_FLAG_TYPE_TRAP32    (0xfU << 0)
#define IDT_FLAG_DPL(_n)        ((_n) << 5)
#define IDT_FLAG_DPL0           IDT_FLAG_DPL(0U)
#define IDT_FLAG_DPL1           IDT_FLAG_DPL(1U)
#define IDT_FLAG_DPL2           IDT_FLAG_DPL(2U)
#define IDT_FLAG_DPL3           IDT_FLAG_DPL(3U)
#define IDT_FLAG_P              (1U << 7)

struct idt {
    struct gate_descriptor entries[256];
};

static void initdescriptor(struct gate_descriptor *out, uint32_t offset, uint16_t flags) {
    memset(out, 0, sizeof(*out));
    out->offset_b15tob0 = offset;
    out->segmentselector = ARCHI586_GDT_KERNEL_CS;
    out->flags = flags;
    out->offset_b31tob16 = offset >> 16;
}

typedef void(handler)(void);

static handler *KERNEL_TRAPS[32] = {
    archi586_isr_exception0entry,  archi586_isr_exception1entry,  archi586_isr_exception2entry,  archi586_isr_exception3entry,
    archi586_isr_exception4entry,  archi586_isr_exception5entry,  archi586_isr_exception6entry,  archi586_isr_exception7entry,
    archi586_isr_exception8entry,  archi586_isr_exception9entry,  archi586_isr_exception10entry, archi586_isr_exception11entry,
    archi586_isr_exception12entry, archi586_isr_exception13entry, archi586_isr_exception14entry, archi586_isr_exception15entry,
    archi586_isr_exception16entry, archi586_isr_exception17entry, archi586_isr_exception18entry, archi586_isr_exception19entry,
    archi586_isr_exception20entry, archi586_isr_exception21entry, archi586_isr_exception22entry, archi586_isr_exception23entry,
    archi586_isr_exception24entry, archi586_isr_exception25entry, archi586_isr_exception26entry, archi586_isr_exception27entry,
    archi586_isr_exception28entry, archi586_isr_exception29entry, archi586_isr_exception30entry, archi586_isr_exception31entry,
};

static handler *KERNEL_INTERRUPT_HANDLERS[] = {
    archi586_isr_interrupt32entry,  archi586_isr_interrupt33entry,  archi586_isr_interrupt34entry,  archi586_isr_interrupt35entry,
    archi586_isr_interrupt36entry,  archi586_isr_interrupt37entry,  archi586_isr_interrupt38entry,  archi586_isr_interrupt39entry,
    archi586_isr_interrupt40entry,  archi586_isr_interrupt41entry,  archi586_isr_interrupt42entry,  archi586_isr_interrupt43entry,
    archi586_isr_interrupt44entry,  archi586_isr_interrupt45entry,  archi586_isr_interrupt46entry,  archi586_isr_interrupt47entry,
    archi586_isr_interrupt48entry,  archi586_isr_interrupt49entry,  archi586_isr_interrupt50entry,  archi586_isr_interrupt51entry,
    archi586_isr_interrupt52entry,  archi586_isr_interrupt53entry,  archi586_isr_interrupt54entry,  archi586_isr_interrupt55entry,
    archi586_isr_interrupt56entry,  archi586_isr_interrupt57entry,  archi586_isr_interrupt58entry,  archi586_isr_interrupt59entry,
    archi586_isr_interrupt60entry,  archi586_isr_interrupt61entry,  archi586_isr_interrupt62entry,  archi586_isr_interrupt63entry,
    archi586_isr_interrupt64entry,  archi586_isr_interrupt65entry,  archi586_isr_interrupt66entry,  archi586_isr_interrupt67entry,
    archi586_isr_interrupt68entry,  archi586_isr_interrupt69entry,  archi586_isr_interrupt70entry,  archi586_isr_interrupt71entry,
    archi586_isr_interrupt72entry,  archi586_isr_interrupt73entry,  archi586_isr_interrupt74entry,  archi586_isr_interrupt75entry,
    archi586_isr_interrupt76entry,  archi586_isr_interrupt77entry,  archi586_isr_interrupt78entry,  archi586_isr_interrupt79entry,
    archi586_isr_interrupt80entry,  archi586_isr_interrupt81entry,  archi586_isr_interrupt82entry,  archi586_isr_interrupt83entry,
    archi586_isr_interrupt84entry,  archi586_isr_interrupt85entry,  archi586_isr_interrupt86entry,  archi586_isr_interrupt87entry,
    archi586_isr_interrupt88entry,  archi586_isr_interrupt89entry,  archi586_isr_interrupt90entry,  archi586_isr_interrupt91entry,
    archi586_isr_interrupt92entry,  archi586_isr_interrupt93entry,  archi586_isr_interrupt94entry,  archi586_isr_interrupt95entry,
    archi586_isr_interrupt96entry,  archi586_isr_interrupt97entry,  archi586_isr_interrupt98entry,  archi586_isr_interrupt99entry,
    archi586_isr_interrupt100entry, archi586_isr_interrupt101entry, archi586_isr_interrupt102entry, archi586_isr_interrupt103entry,
    archi586_isr_interrupt104entry, archi586_isr_interrupt105entry, archi586_isr_interrupt106entry, archi586_isr_interrupt107entry,
    archi586_isr_interrupt108entry, archi586_isr_interrupt109entry, archi586_isr_interrupt110entry, archi586_isr_interrupt111entry,
    archi586_isr_interrupt112entry, archi586_isr_interrupt113entry, archi586_isr_interrupt114entry, archi586_isr_interrupt115entry,
    archi586_isr_interrupt116entry, archi586_isr_interrupt117entry, archi586_isr_interrupt118entry, archi586_isr_interrupt119entry,
    archi586_isr_interrupt120entry, archi586_isr_interrupt121entry, archi586_isr_interrupt122entry, archi586_isr_interrupt123entry,
    archi586_isr_interrupt124entry, archi586_isr_interrupt125entry, archi586_isr_interrupt126entry, archi586_isr_interrupt127entry,
    archi586_isr_interrupt128entry, archi586_isr_interrupt129entry, archi586_isr_interrupt130entry, archi586_isr_interrupt131entry,
    archi586_isr_interrupt132entry, archi586_isr_interrupt133entry, archi586_isr_interrupt134entry, archi586_isr_interrupt135entry,
    archi586_isr_interrupt136entry, archi586_isr_interrupt137entry, archi586_isr_interrupt138entry, archi586_isr_interrupt139entry,
    archi586_isr_interrupt140entry, archi586_isr_interrupt141entry, archi586_isr_interrupt142entry, archi586_isr_interrupt143entry,
    archi586_isr_interrupt144entry, archi586_isr_interrupt145entry, archi586_isr_interrupt146entry, archi586_isr_interrupt147entry,
    archi586_isr_interrupt148entry, archi586_isr_interrupt149entry, archi586_isr_interrupt150entry, archi586_isr_interrupt151entry,
    archi586_isr_interrupt152entry, archi586_isr_interrupt153entry, archi586_isr_interrupt154entry, archi586_isr_interrupt155entry,
    archi586_isr_interrupt156entry, archi586_isr_interrupt157entry, archi586_isr_interrupt158entry, archi586_isr_interrupt159entry,
    archi586_isr_interrupt160entry, archi586_isr_interrupt161entry, archi586_isr_interrupt162entry, archi586_isr_interrupt163entry,
    archi586_isr_interrupt164entry, archi586_isr_interrupt165entry, archi586_isr_interrupt166entry, archi586_isr_interrupt167entry,
    archi586_isr_interrupt168entry, archi586_isr_interrupt169entry, archi586_isr_interrupt170entry, archi586_isr_interrupt171entry,
    archi586_isr_interrupt172entry, archi586_isr_interrupt173entry, archi586_isr_interrupt174entry, archi586_isr_interrupt175entry,
    archi586_isr_interrupt176entry, archi586_isr_interrupt177entry, archi586_isr_interrupt178entry, archi586_isr_interrupt179entry,
    archi586_isr_interrupt180entry, archi586_isr_interrupt181entry, archi586_isr_interrupt182entry, archi586_isr_interrupt183entry,
    archi586_isr_interrupt184entry, archi586_isr_interrupt185entry, archi586_isr_interrupt186entry, archi586_isr_interrupt187entry,
    archi586_isr_interrupt188entry, archi586_isr_interrupt189entry, archi586_isr_interrupt190entry, archi586_isr_interrupt191entry,
    archi586_isr_interrupt192entry, archi586_isr_interrupt193entry, archi586_isr_interrupt194entry, archi586_isr_interrupt195entry,
    archi586_isr_interrupt196entry, archi586_isr_interrupt197entry, archi586_isr_interrupt198entry, archi586_isr_interrupt199entry,
    archi586_isr_interrupt200entry, archi586_isr_interrupt201entry, archi586_isr_interrupt202entry, archi586_isr_interrupt203entry,
    archi586_isr_interrupt204entry, archi586_isr_interrupt205entry, archi586_isr_interrupt206entry, archi586_isr_interrupt207entry,
    archi586_isr_interrupt208entry, archi586_isr_interrupt209entry, archi586_isr_interrupt210entry, archi586_isr_interrupt211entry,
    archi586_isr_interrupt212entry, archi586_isr_interrupt213entry, archi586_isr_interrupt214entry, archi586_isr_interrupt215entry,
    archi586_isr_interrupt216entry, archi586_isr_interrupt217entry, archi586_isr_interrupt218entry, archi586_isr_interrupt219entry,
    archi586_isr_interrupt220entry, archi586_isr_interrupt221entry, archi586_isr_interrupt222entry, archi586_isr_interrupt223entry,
    archi586_isr_interrupt224entry, archi586_isr_interrupt225entry, archi586_isr_interrupt226entry, archi586_isr_interrupt227entry,
    archi586_isr_interrupt228entry, archi586_isr_interrupt229entry, archi586_isr_interrupt230entry, archi586_isr_interrupt231entry,
    archi586_isr_interrupt232entry, archi586_isr_interrupt233entry, archi586_isr_interrupt234entry, archi586_isr_interrupt235entry,
    archi586_isr_interrupt236entry, archi586_isr_interrupt237entry, archi586_isr_interrupt238entry, archi586_isr_interrupt239entry,
    archi586_isr_interrupt240entry, archi586_isr_interrupt241entry, archi586_isr_interrupt242entry, archi586_isr_interrupt243entry,
    archi586_isr_interrupt244entry, archi586_isr_interrupt245entry, archi586_isr_interrupt246entry, archi586_isr_interrupt247entry,
    archi586_isr_interrupt248entry, archi586_isr_interrupt249entry, archi586_isr_interrupt250entry, archi586_isr_interrupt251entry,
    archi586_isr_interrupt252entry, archi586_isr_interrupt253entry, archi586_isr_interrupt254entry, archi586_isr_interrupt255entry,
};

static struct idt s_idt __attribute__((section(".data.ro_after_early_init")));

void archi586_idt_init(void) {
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

void archi586_idt_load(void) {
    struct idtr {
        uint16_t size;
        uint32_t offset;
    } __attribute__((packed));

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
        "idiv %eax\n"
    );
}
