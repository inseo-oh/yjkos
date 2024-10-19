#pragma once
#include <kernel/lib/list.h>
#include <stdint.h>

struct traphandler {
    void (*callback)(int trapnum, void *trapframe, void *data);
    void *data;
    uint32_t checksum;
    struct list_node node;
};

// NOTE: On i586, you probably want to use archi586_pic_registerhandler instead, because PIC driver registers
//       its own traphandler for all of its IRQs, and also takes care of suprious IRQ handling.
void trapmanager_register(struct traphandler *out, int trapnum, void (*callback)(int trapnum, void *trapframe, void *data), void *data);
void trapmanager_trap(int trapnum, void *trapframe);
