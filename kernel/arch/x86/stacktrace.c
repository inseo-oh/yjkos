#include "exceptions.h"
#include <kernel/arch/mmu.h>
#include <kernel/arch/stacktrace.h>
#include <kernel/io/tty.h>
#include <kernel/status.h>
#include <kernel/types.h>
#include <stddef.h>
#include <stdint.h>

typedef struct funcstackframe funcstackframe_t;
struct funcstackframe {
    funcstackframe_t *next;
    uint32_t eip;
};

static void stacktrace_with_frame(funcstackframe_t *startingframe) {
    tty_printf("stack trace:\n");
    funcstackframe_t *frame = startingframe;
    while (frame != NULL) {
        physptr_t physaddr;
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
    tty_printf("pc: %#lx\n", ((trapframe_t *)trapframe)->eip);
    stacktrace_with_frame((void *)((trapframe_t *)trapframe)->ebp);
}

void arch_stacktrace(void) {
    funcstackframe_t *frame;
    __asm__ ("mov %%ebp, %0" : "=r"(frame));
    stacktrace_with_frame(frame);
}
