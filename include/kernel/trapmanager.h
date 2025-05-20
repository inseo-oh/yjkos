#pragma once
#include <kernel/lib/list.h>
#include <stdint.h>

struct trap_handler {
    void (*callback)(int trapnum, void *trapframe, void *data);
    void *data;
    uint32_t checksum;
    struct list_node node;
};

/* 
 * NOTE: On i586, you probably want to use archi586_pic_register_handler instead.
 * That is because PIC driver registers its own traphandler for all of its IRQs, and also takes care of suprious IRQ handling.
 */
void trapmanager_register_trap(struct trap_handler *out, int trapnum, void (*callback)(int trapnum, void *trapframe, void *data), void *data);
void trapmanager_trap(int trapnum, void *trapframe);
