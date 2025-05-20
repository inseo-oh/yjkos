#pragma once
#include <kernel/lib/list.h>
#include <stdint.h>

struct archi586_pic_irq_handler {
    void (*callback)(int irqnum, void *data);
    void *data;
    struct list_node node;
};

void archi586_pic_send_eoi(uint8_t irq);
bool archi586_pic_is_irq_masked(uint8_t irq);
void archi586_pic_mask_irq(uint8_t irq);
void archi586_pic_unmask_irq(uint8_t irq);
void archi586_pic_init(void);
/* NOTE: Each handler is responsible for sending EOI. This is to support cases where EOI is not sent at the end of handler, like timer IRQ. */
void archi586_pic_register_handler(struct archi586_pic_irq_handler *out, int irqnum, void (*callback)(int irqnum, void *data), void *data);
