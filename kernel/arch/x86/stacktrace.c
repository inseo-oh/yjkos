#include "exceptions.h"
#include <kernel/arch/mmu.h>
#include <kernel/arch/stacktrace.h>
#include <kernel/io/tty.h>
#include <kernel/status.h>
#include <kernel/types.h>
#include <stddef.h>
#include <stdint.h>

struct funcstackframe {
    struct funcstackframe *next;
    uint32_t eip;
};

static void stacktrace_with_frame(struct funcstackframe *startingframe) {
    tty_printf("stack trace:\n");
    struct funcstackframe *frame = startingframe;
    while (frame != NULL) {
        physptr physaddr;
        status_t status = arch_mmu_virttophys(&physaddr, (uintptr_t)frame);
        if (status != OK) {
            tty_printf("  stack frame at %p is not accessible. STOP.\n", frame);
            break;
        }
        tty_printf("  %#lx\n", frame->eip);
        frame = frame->next;
    }
}

void arch_stacktrace_for_trapframe(void *trapframe) {
    if (trapframe == NULL) {
        tty_printf("stack trace:\n  <no trace info available>\n");
        return;
    }
    tty_printf("pc: %#lx\n", ((struct trapframe *)trapframe)->eip);
    stacktrace_with_frame((void *)((struct trapframe *)trapframe)->ebp);
}

void arch_stacktrace(void) {
    struct funcstackframe *frame;
    __asm__ ("mov %%ebp, %0" : "=r"(frame));
    stacktrace_with_frame(frame);
}
