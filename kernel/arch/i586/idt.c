#include "idt.h"
#include "asm/interruptentry.h"
#include "gdt.h"
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
    ArchI586_ISR_Exception0Entry,
    ArchI586_ISR_Exception1Entry,
    ArchI586_ISR_Exception2Entry,
    ArchI586_ISR_Exception3Entry,
    ArchI586_ISR_Exception4Entry,
    ArchI586_ISR_Exception5Entry,
    ArchI586_ISR_Exception6Entry,
    ArchI586_ISR_Exception7Entry,
    ArchI586_ISR_Exception8Entry,
    ArchI586_ISR_Exception9Entry,
    ArchI586_ISR_Exception10Entry,
    ArchI586_ISR_Exception11Entry,
    ArchI586_ISR_Exception12Entry,
    ArchI586_ISR_Exception13Entry,
    ArchI586_ISR_Exception14Entry,
    ArchI586_ISR_Exception15Entry,
    ArchI586_ISR_Exception16Entry,
    ArchI586_ISR_Exception17Entry,
    ArchI586_ISR_Exception18Entry,
    ArchI586_ISR_Exception19Entry,
    ArchI586_ISR_Exception20Entry,
    ArchI586_ISR_Exception21Entry,
    ArchI586_ISR_Exception22Entry,
    ArchI586_ISR_Exception23Entry,
    ArchI586_ISR_Exception24Entry,
    ArchI586_ISR_Exception25Entry,
    ArchI586_ISR_Exception26Entry,
    ArchI586_ISR_Exception27Entry,
    ArchI586_ISR_Exception28Entry,
    ArchI586_ISR_Exception29Entry,
    ArchI586_ISR_Exception30Entry,
    ArchI586_ISR_Exception31Entry,
};

static HANDLER *KERNEL_INTERRUPT_HANDLERS[] = {
    ArchI586_ISR_Interrupt32Entry,
    ArchI586_ISR_Interrupt33Entry,
    ArchI586_ISR_Interrupt34Entry,
    ArchI586_ISR_Interrupt35Entry,
    ArchI586_ISR_Interrupt36Entry,
    ArchI586_ISR_Interrupt37Entry,
    ArchI586_ISR_Interrupt38Entry,
    ArchI586_ISR_Interrupt39Entry,
    ArchI586_ISR_Interrupt40Entry,
    ArchI586_ISR_Interrupt41Entry,
    ArchI586_ISR_Interrupt42Entry,
    ArchI586_ISR_Interrupt43Entry,
    ArchI586_ISR_Interrupt44Entry,
    ArchI586_ISR_Interrupt45Entry,
    ArchI586_ISR_Interrupt46Entry,
    ArchI586_ISR_Interrupt47Entry,
    ArchI586_ISR_Interrupt48Entry,
    ArchI586_ISR_Interrupt49Entry,
    ArchI586_ISR_Interrupt50Entry,
    ArchI586_ISR_Interrupt51Entry,
    ArchI586_ISR_Interrupt52Entry,
    ArchI586_ISR_Interrupt53Entry,
    ArchI586_ISR_Interrupt54Entry,
    ArchI586_ISR_Interrupt55Entry,
    ArchI586_ISR_Interrupt56Entry,
    ArchI586_ISR_Interrupt57Entry,
    ArchI586_ISR_Interrupt58Entry,
    ArchI586_ISR_Interrupt59Entry,
    ArchI586_ISR_Interrupt60Entry,
    ArchI586_ISR_Interrupt61Entry,
    ArchI586_ISR_Interrupt62Entry,
    ArchI586_ISR_Interrupt63Entry,
    ArchI586_ISR_Interrupt64Entry,
    ArchI586_ISR_Interrupt65Entry,
    ArchI586_ISR_Interrupt66Entry,
    ArchI586_ISR_Interrupt67Entry,
    ArchI586_ISR_Interrupt68Entry,
    ArchI586_ISR_Interrupt69Entry,
    ArchI586_ISR_Interrupt70Entry,
    ArchI586_ISR_Interrupt71Entry,
    ArchI586_ISR_Interrupt72Entry,
    ArchI586_ISR_Interrupt73Entry,
    ArchI586_ISR_Interrupt74Entry,
    ArchI586_ISR_Interrupt75Entry,
    ArchI586_ISR_Interrupt76Entry,
    ArchI586_ISR_Interrupt77Entry,
    ArchI586_ISR_Interrupt78Entry,
    ArchI586_ISR_Interrupt79Entry,
    ArchI586_ISR_Interrupt80Entry,
    ArchI586_ISR_Interrupt81Entry,
    ArchI586_ISR_Interrupt82Entry,
    ArchI586_ISR_Interrupt83Entry,
    ArchI586_ISR_Interrupt84Entry,
    ArchI586_ISR_Interrupt85Entry,
    ArchI586_ISR_Interrupt86Entry,
    ArchI586_ISR_Interrupt87Entry,
    ArchI586_ISR_Interrupt88Entry,
    ArchI586_ISR_Interrupt89Entry,
    ArchI586_ISR_Interrupt90Entry,
    ArchI586_ISR_Interrupt91Entry,
    ArchI586_ISR_Interrupt92Entry,
    ArchI586_ISR_Interrupt93Entry,
    ArchI586_ISR_Interrupt94Entry,
    ArchI586_ISR_Interrupt95Entry,
    ArchI586_ISR_Interrupt96Entry,
    ArchI586_ISR_Interrupt97Entry,
    ArchI586_ISR_Interrupt98Entry,
    ArchI586_ISR_Interrupt99Entry,
    ArchI586_ISR_Interrupt100Entry,
    ArchI586_ISR_Interrupt101Entry,
    ArchI586_ISR_Interrupt102Entry,
    ArchI586_ISR_Interrupt103Entry,
    ArchI586_ISR_Interrupt104Entry,
    ArchI586_ISR_Interrupt105Entry,
    ArchI586_ISR_Interrupt106Entry,
    ArchI586_ISR_Interrupt107Entry,
    ArchI586_ISR_Interrupt108Entry,
    ArchI586_ISR_Interrupt109Entry,
    ArchI586_ISR_Interrupt110Entry,
    ArchI586_ISR_Interrupt111Entry,
    ArchI586_ISR_Interrupt112Entry,
    ArchI586_ISR_Interrupt113Entry,
    ArchI586_ISR_Interrupt114Entry,
    ArchI586_ISR_Interrupt115Entry,
    ArchI586_ISR_Interrupt116Entry,
    ArchI586_ISR_Interrupt117Entry,
    ArchI586_ISR_Interrupt118Entry,
    ArchI586_ISR_Interrupt119Entry,
    ArchI586_ISR_Interrupt120Entry,
    ArchI586_ISR_Interrupt121Entry,
    ArchI586_ISR_Interrupt122Entry,
    ArchI586_ISR_Interrupt123Entry,
    ArchI586_ISR_Interrupt124Entry,
    ArchI586_ISR_Interrupt125Entry,
    ArchI586_ISR_Interrupt126Entry,
    ArchI586_ISR_Interrupt127Entry,
    ArchI586_ISR_Interrupt128Entry,
    ArchI586_ISR_Interrupt129Entry,
    ArchI586_ISR_Interrupt130Entry,
    ArchI586_ISR_Interrupt131Entry,
    ArchI586_ISR_Interrupt132Entry,
    ArchI586_ISR_Interrupt133Entry,
    ArchI586_ISR_Interrupt134Entry,
    ArchI586_ISR_Interrupt135Entry,
    ArchI586_ISR_Interrupt136Entry,
    ArchI586_ISR_Interrupt137Entry,
    ArchI586_ISR_Interrupt138Entry,
    ArchI586_ISR_Interrupt139Entry,
    ArchI586_ISR_Interrupt140Entry,
    ArchI586_ISR_Interrupt141Entry,
    ArchI586_ISR_Interrupt142Entry,
    ArchI586_ISR_Interrupt143Entry,
    ArchI586_ISR_Interrupt144Entry,
    ArchI586_ISR_Interrupt145Entry,
    ArchI586_ISR_Interrupt146Entry,
    ArchI586_ISR_Interrupt147Entry,
    ArchI586_ISR_Interrupt148Entry,
    ArchI586_ISR_Interrupt149Entry,
    ArchI586_ISR_Interrupt150Entry,
    ArchI586_ISR_Interrupt151Entry,
    ArchI586_ISR_Interrupt152Entry,
    ArchI586_ISR_Interrupt153Entry,
    ArchI586_ISR_Interrupt154Entry,
    ArchI586_ISR_Interrupt155Entry,
    ArchI586_ISR_Interrupt156Entry,
    ArchI586_ISR_Interrupt157Entry,
    ArchI586_ISR_Interrupt158Entry,
    ArchI586_ISR_Interrupt159Entry,
    ArchI586_ISR_Interrupt160Entry,
    ArchI586_ISR_Interrupt161Entry,
    ArchI586_ISR_Interrupt162Entry,
    ArchI586_ISR_Interrupt163Entry,
    ArchI586_ISR_Interrupt164Entry,
    ArchI586_ISR_Interrupt165Entry,
    ArchI586_ISR_Interrupt166Entry,
    ArchI586_ISR_Interrupt167Entry,
    ArchI586_ISR_Interrupt168Entry,
    ArchI586_ISR_Interrupt169Entry,
    ArchI586_ISR_Interrupt170Entry,
    ArchI586_ISR_Interrupt171Entry,
    ArchI586_ISR_Interrupt172Entry,
    ArchI586_ISR_Interrupt173Entry,
    ArchI586_ISR_Interrupt174Entry,
    ArchI586_ISR_Interrupt175Entry,
    ArchI586_ISR_Interrupt176Entry,
    ArchI586_ISR_Interrupt177Entry,
    ArchI586_ISR_Interrupt178Entry,
    ArchI586_ISR_Interrupt179Entry,
    ArchI586_ISR_Interrupt180Entry,
    ArchI586_ISR_Interrupt181Entry,
    ArchI586_ISR_Interrupt182Entry,
    ArchI586_ISR_Interrupt183Entry,
    ArchI586_ISR_Interrupt184Entry,
    ArchI586_ISR_Interrupt185Entry,
    ArchI586_ISR_Interrupt186Entry,
    ArchI586_ISR_Interrupt187Entry,
    ArchI586_ISR_Interrupt188Entry,
    ArchI586_ISR_Interrupt189Entry,
    ArchI586_ISR_Interrupt190Entry,
    ArchI586_ISR_Interrupt191Entry,
    ArchI586_ISR_Interrupt192Entry,
    ArchI586_ISR_Interrupt193Entry,
    ArchI586_ISR_Interrupt194Entry,
    ArchI586_ISR_Interrupt195Entry,
    ArchI586_ISR_Interrupt196Entry,
    ArchI586_ISR_Interrupt197Entry,
    ArchI586_ISR_Interrupt198Entry,
    ArchI586_ISR_Interrupt199Entry,
    ArchI586_ISR_Interrupt200Entry,
    ArchI586_ISR_Interrupt201Entry,
    ArchI586_ISR_Interrupt202Entry,
    ArchI586_ISR_Interrupt203Entry,
    ArchI586_ISR_Interrupt204Entry,
    ArchI586_ISR_Interrupt205Entry,
    ArchI586_ISR_Interrupt206Entry,
    ArchI586_ISR_Interrupt207Entry,
    ArchI586_ISR_Interrupt208Entry,
    ArchI586_ISR_Interrupt209Entry,
    ArchI586_ISR_Interrupt210Entry,
    ArchI586_ISR_Interrupt211Entry,
    ArchI586_ISR_Interrupt212Entry,
    ArchI586_ISR_Interrupt213Entry,
    ArchI586_ISR_Interrupt214Entry,
    ArchI586_ISR_Interrupt215Entry,
    ArchI586_ISR_Interrupt216Entry,
    ArchI586_ISR_Interrupt217Entry,
    ArchI586_ISR_Interrupt218Entry,
    ArchI586_ISR_Interrupt219Entry,
    ArchI586_ISR_Interrupt220Entry,
    ArchI586_ISR_Interrupt221Entry,
    ArchI586_ISR_Interrupt222Entry,
    ArchI586_ISR_Interrupt223Entry,
    ArchI586_ISR_Interrupt224Entry,
    ArchI586_ISR_Interrupt225Entry,
    ArchI586_ISR_Interrupt226Entry,
    ArchI586_ISR_Interrupt227Entry,
    ArchI586_ISR_Interrupt228Entry,
    ArchI586_ISR_Interrupt229Entry,
    ArchI586_ISR_Interrupt230Entry,
    ArchI586_ISR_Interrupt231Entry,
    ArchI586_ISR_Interrupt232Entry,
    ArchI586_ISR_Interrupt233Entry,
    ArchI586_ISR_Interrupt234Entry,
    ArchI586_ISR_Interrupt235Entry,
    ArchI586_ISR_Interrupt236Entry,
    ArchI586_ISR_Interrupt237Entry,
    ArchI586_ISR_Interrupt238Entry,
    ArchI586_ISR_Interrupt239Entry,
    ArchI586_ISR_Interrupt240Entry,
    ArchI586_ISR_Interrupt241Entry,
    ArchI586_ISR_Interrupt242Entry,
    ArchI586_ISR_Interrupt243Entry,
    ArchI586_ISR_Interrupt244Entry,
    ArchI586_ISR_Interrupt245Entry,
    ArchI586_ISR_Interrupt246Entry,
    ArchI586_ISR_Interrupt247Entry,
    ArchI586_ISR_Interrupt248Entry,
    ArchI586_ISR_Interrupt249Entry,
    ArchI586_ISR_Interrupt250Entry,
    ArchI586_ISR_Interrupt251Entry,
    ArchI586_ISR_Interrupt252Entry,
    ArchI586_ISR_Interrupt253Entry,
    ArchI586_ISR_Interrupt254Entry,
    ArchI586_ISR_Interrupt255Entry,
};

static struct idt s_idt __attribute__((section(".data.ro_after_early_init")));

void ArchI586_Idt_Init(void) {
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

void ArchI586_Idt_Load(void) {
    struct idtr {
        uint16_t size;
        uint32_t offset;
    } __attribute__((packed));

    volatile struct idtr idtr;
    idtr.offset = (uintptr_t)&s_idt;
    idtr.size = sizeof(s_idt);
    __asm__ volatile("lidt (%0)" ::"r"(&idtr));
}

void ArchI586_Idt_Test(void) {
    Co_Printf("triggering divide by zero for testing\n");
    __asm__ volatile(
        "mov $0, %eax\n"
        "mov $0x11111111, %edi\n"
        "mov $0x22222222, %esi\n"
        "mov $0x44444444, %ebx\n"
        "mov $0x55555555, %edx\n"
        "mov $0x66666666, %ecx\n"
        "idiv %eax\n");
}
