#include "pic.h"
#include "ioport.h"
#include <assert.h>
#include <kernel/arch/interrupts.h>
#include <kernel/io/co.h>
#include <kernel/lib/list.h>
#include <kernel/trapmanager.h>
#include <stddef.h>
#include <stdint.h>

#define CMDPORT_MASTER 0x20
#define DATAPORT_MASTER 0x21
#define CMDPORT_SLAVE 0xa0
#define DATAPORT_SLAVE 0xa1

#define CMD_READIRR 0x0a
#define CMD_READISR 0x0b
#define CMD_EOI 0x20

#define SLAVEPIN_ON_MASTER 2
#define IRQS_PER_PIC 8
#define IRQS_TOTAL (IRQS_PER_PIC * 2)
#define PIC_VECTOR_BASE 0x20

static uint8_t const PIC_ICW1_FLAG_ICW4 = 1 << 0;
static uint8_t const PIC_ICW1_FLAG_INIT = 1 << 4;
static uint8_t const PIC_ICW4_FLAG_8086MODE = 1 << 0;

static uint16_t readirqreg(uint8_t readcmd) {
    ArchI586_Out8(CMDPORT_MASTER, readcmd);
    ArchI586_Out8(CMDPORT_SLAVE, readcmd);
    return ((uint32_t)ArchI586_In8(CMDPORT_SLAVE) << 8) | ArchI586_In8(CMDPORT_MASTER);
}

static uint16_t readirr(void) {
    return readirqreg(CMD_READIRR);
}

static uint16_t readisr(void) {
    return readirqreg(CMD_READISR);
}

static uint16_t getirqmask(void) {
    uint32_t master_mask = ArchI586_In8(DATAPORT_MASTER);
    uint32_t slave_mask = ArchI586_In8(DATAPORT_SLAVE);
    return (slave_mask << 8) | master_mask;
}

static void setirqmask(uint16_t mask) {
    ArchI586_Out8(DATAPORT_MASTER, mask);
    ArchI586_Out8(DATAPORT_SLAVE, mask >> 8);
}

void ArchI586_Pic_SendEoi(uint8_t irq) {
    if (irq >= IRQS_PER_PIC) {
        ArchI586_Out8(CMDPORT_SLAVE, CMD_EOI);
    }
    ArchI586_Out8(CMDPORT_MASTER, CMD_EOI);
}

static bool checkspuriousirq(uint8_t irq) {
    bool is_real = readisr() & (1U << irq);
    if (irq == 7) {
        if (!is_real) {
            Co_Printf("pic: spurious irq %u received\n", irq);
        }
        return is_real;
    }
    if (irq == 15) {
        if (!is_real) {
            Co_Printf("pic: spurious irq %u received\n", irq);
            /*
             * If spurious IRQ occured on the slave PIC, master PIC has no idea
             * that it is spurious at all.
             * So we must send EOI to the master.
             */
            ArchI586_Pic_SendEoi(SLAVEPIN_ON_MASTER);
        }
        return is_real;
    }
    return true;
}

bool ArchI586_Pic_IsIrqMasked(uint8_t irq) {
    return getirqmask() & (1U << irq);
}

void ArchI586_Pic_MaskIrq(uint8_t irq) {
    setirqmask(getirqmask() | (1U << irq));
}

void ArchI586_Pic_UnmaskIrq(uint8_t irq) {
    setirqmask(getirqmask() & ~(1U << irq));
}

static struct TrapHandler s_traphandler[IRQS_TOTAL];

/* Each IRQ entry is a list of IRQ handlers. */
static struct List s_irqs[IRQS_TOTAL];

/* Default handler that just EOIs given IRQ */
static void default_irq_handler(int trapnum, void *trapframe, void *data) {
    (void)trapframe;
    (void)data;
    int irqnum = trapnum - PIC_VECTOR_BASE;
    assert(irqnum < IRQS_TOTAL);
    bool is_suprious_irq = checkspuriousirq(irqnum);
    if (s_irqs[irqnum].front == NULL) {
        Co_Printf("no irq handler registered for irq %d\n", trapnum);
        return;
    }
    LIST_FOREACH(&s_irqs[irqnum], handlernode) {
        struct ArchI586_Pic_IrqHandler *handler = handlernode->data;
        assert(handler != NULL);
        handler->callback(irqnum, handler->data);
    }
    if (!is_suprious_irq) {
        ArchI586_Pic_SendEoi(irqnum);
    }
}

void ArchI586_Pic_Init(void) {
    /* ICW1 *******************************************************************/
    ArchI586_Out8(CMDPORT_MASTER, PIC_ICW1_FLAG_INIT | PIC_ICW1_FLAG_ICW4);
    ArchI586_Out8(CMDPORT_SLAVE, PIC_ICW1_FLAG_INIT | PIC_ICW1_FLAG_ICW4);
    /* ICW2 *******************************************************************/
    ArchI586_Out8(DATAPORT_MASTER, PIC_VECTOR_BASE);
    ArchI586_Out8(DATAPORT_SLAVE, PIC_VECTOR_BASE + IRQS_PER_PIC);
    /* ICW3 *******************************************************************/
    ArchI586_Out8(DATAPORT_MASTER, 1 << SLAVEPIN_ON_MASTER);
    ArchI586_Out8(DATAPORT_SLAVE, SLAVEPIN_ON_MASTER);
    /* ICW4 *******************************************************************/
    ArchI586_Out8(DATAPORT_MASTER, PIC_ICW4_FLAG_8086MODE);
    ArchI586_Out8(DATAPORT_SLAVE, PIC_ICW4_FLAG_8086MODE);
    /* Setup default PIC handler **********************************************/
    for (size_t i = 0; i < 8; i++) {
        TrapManager_Register(&s_traphandler[i], PIC_VECTOR_BASE + i, default_irq_handler, NULL);
        TrapManager_Register(&s_traphandler[IRQS_PER_PIC + i], PIC_VECTOR_BASE + IRQS_PER_PIC + i, default_irq_handler, NULL);
    }
    /* Disable IRQs except for IRQ2(which is connected to slave PIC) **********/
    setirqmask(~(uint16_t)(1U << 2));
}

void ArchI586_Pic_RegisterHandler(struct ArchI586_Pic_IrqHandler *out, int irqnum, void (*callback)(int irqnum, void *data), void *data) {
    bool prev_interrupts = Arch_Irq_Disable();
    out->callback = callback;
    out->data = data;
    List_InsertBack(&s_irqs[irqnum], &out->node, out);
    Arch_Irq_Restore(prev_interrupts);
}
