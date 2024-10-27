#include "asm/i586.h"
#include "exceptions.h"
#include <kernel/arch/hcf.h>
#include <kernel/arch/stacktrace.h>
#include <kernel/io/co.h>
#include <kernel/mem/vmm.h>
#include <kernel/trapmanager.h>
#include <stddef.h>
#include <stdint.h>

static void dumptrapframe(struct trapframe *self) {
    co_printf("eax=%08lx ebx=%08lx ecx=%08lx edx=%08lx esi=%08lx edi=%08lx\n", self->eax, self->ebx, self->ecx, self->edx, self->esi, self->edi);
    co_printf("ebp=%08lx eip=%08lx efl=%08lx cs =%08lx ds =%08lx es =%08lx\n", self->ebp, self->eip, self->eflags, self->cs, self->ds, self->es);
    co_printf("fs =%08lx gs =%08lx\n", self->fs, self->gs);
    arch_stacktrace_for_trapframe(self);
}

static void defaulthandler(int trapnum, void *trapframe, void *data) {
    (void)data;

    struct trapframe *frame = trapframe;
    co_printf("fatal exception %d occured (error code %#x)\n", trapnum, frame->errcode);
    dumptrapframe(frame);
    arch_hcf();
}

static void pagefaulthandler(int trapnum, void *trapframe, void *data) {
    (void)data;
    (void)trapnum;
    enum {
        PF_FLAG_P = 1 << 0, // Present
        PF_FLAG_W = 1 << 1, // write
        PF_FLAG_U = 1 << 2, // User
    };

    struct trapframe *frame = trapframe;
    uintptr_t faultaddr = archi586_readcr2();
    vmm_pagefault(faultaddr, frame->errcode & PF_FLAG_P, frame->errcode & PF_FLAG_W, frame->errcode & PF_FLAG_U, frame);
}

static struct traphandler s_traphandler[32];

void archi586_exceptions_init(void) {
    for (int i = 0; i < 32; i++) {
        switch(i) {
            case 14:
                trapmanager_register(&s_traphandler[i], i, pagefaulthandler, NULL);
                break;
            default:
                trapmanager_register(&s_traphandler[i], i, defaulthandler, NULL);
        }
    }
}
