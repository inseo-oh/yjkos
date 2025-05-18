#pragma once
#include <kernel/lib/list.h>
#include <stdint.h>

struct ArchI586_Pic_IrqHandler {
    void (*callback)(int irqnum, void *data);
    void *data;
    struct List_Node node;
};

void ArchI586_Pic_SendEoi(uint8_t irq);
bool ArchI586_Pic_IsIrqMasked(uint8_t irq);
void ArchI586_Pic_MaskIrq(uint8_t irq);
void ArchI586_Pic_UnmaskIrq(uint8_t irq);
void ArchI586_Pic_Init(void);
/* NOTE: Each handler is responsible for sending EOI. This is to support cases where EOI is not sent at the end of handler, like timer IRQ. */
void ArchI586_Pic_RegisterHandler(struct ArchI586_Pic_IrqHandler *out, int irqnum, void (*callback)(int irqnum, void *data), void *data);
