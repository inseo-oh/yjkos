#pragma once
#include <kernel/lib/list.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct archx86_pic_irqhandler archx86_pic_irqhandler_t;
struct archx86_pic_irqhandler {
    void (*callback)(int irqnum, void *data);
    void *data;
    list_node_t node;
};

void archx86_pic_sendeoi(uint8_t irq);
bool archx86_pic_isirqmasked(uint8_t irq);
void archx86_pic_maskirq(uint8_t irq);
void archx86_pic_unmaskirq(uint8_t irq);
void archx86_pic_init(void);
// NOTE: Each handler is responsible for sending EOI. This is to support cases where EOI is not sent at the
//       end of handler, like timer IRQ.
void archx86_pic_registerhandler(archx86_pic_irqhandler_t *out, int irqnum, void (*callback)(int irqnum, void *data), void *data);
