#pragma once
#include <kernel/lib/list.h>
#include <stdint.h>

struct TrapHandler {
    void (*callback)(int trapnum, void *trapframe, void *data);
    void *data;
    uint32_t checksum;
    struct List_Node node;
};

/* 
 * NOTE: On i586, you probably want to use ArchI586_Pic_RegisterHandler instead.
 * That is because PIC driver registers its own traphandler for all of its IRQs, and also takes care of suprious IRQ handling.
 */
void TrapManager_Register(struct TrapHandler *out, int trapnum, void (*callback)(int trapnum, void *trapframe, void *data), void *data);
void TrapManager_Trap(int trapnum, void *trapframe);
