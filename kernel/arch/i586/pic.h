#pragma once
#include <kernel/lib/list.h>
#include <stdbool.h>
#include <stdint.h>

struct archi586_pic_irqhandler {
    void (*callback)(int irqnum, void *data);
    void *data;
    struct list_node node;
};

void archi586_pic_sendeoi(uint8_t irq);
bool archi586_pic_isirqmasked(uint8_t irq);
void archi586_pic_maskirq(uint8_t irq);
void archi586_pic_unmaskirq(uint8_t irq);
void archi586_pic_init(void);
// NOTE: Each handler is responsible for sending EOI. This is to support cases where EOI is not sent at the
//       end of handler, like timer IRQ.
void archi586_pic_registerhandler(struct archi586_pic_irqhandler *out, int irqnum, void (*callback)(int irqnum, void *data), void *data);
