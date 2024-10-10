#pragma once
#include <kernel/lib/list.h>
#include <stdint.h>

typedef struct traphandler traphandler_t;
struct traphandler {
    void (*callback)(int trapnum, void *trapframe, void *data);
    void *data;
    uint32_t checksum;
    list_node_t node;
};

// NOTE: On x86, you probably want to use archx86_pic_registerhandler instead, because PIC driver registers
//       its own traphandler for all of its IRQs, and also takes care of suprious IRQ handling.
void trapmanager_register(traphandler_t *out, int trapnum, void (*callback)(int trapnum, void *trapframe, void *data), void *data);
void trapmanager_trap(int trapnum, void *trapframe);
