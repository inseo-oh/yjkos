#include "exceptions.h"
#include <kernel/arch/mmu.h>
#include <kernel/arch/stacktrace.h>
#include <kernel/io/co.h>
#include <kernel/types.h>
#include <stddef.h>
#include <stdint.h>

struct funcstackframe {
    struct funcstackframe *next;
    uint32_t eip;
};

static void stacktrace_with_frame(struct funcstackframe *startingframe) {
    co_printf("stack trace:\n");
    struct funcstackframe *frame = startingframe;
    while (frame != NULL) {
        PHYSPTR physaddr;
        int ret = arch_mmu_virtual_to_physical(&physaddr, frame);
        if (ret < 0) {
            co_printf("  stackframe at %p inaccessible(error %d) - STOP.\n", ret, frame);
            break;
        }
        co_printf("  %#lx\n", frame->eip);
        frame = frame->next;
    }
}

void arch_stacktrace_for_trapframe(void *trapframe) {
    if (trapframe == NULL) {
        co_printf("stack trace:\n  <no trace info available>\n");
        return;
    }
    co_printf("pc: %#lx\n", ((struct trap_frame *)trapframe)->eip);
    stacktrace_with_frame((void *)((struct trap_frame *)trapframe)->ebp);
}

void arch_stacktrace(void) {
    struct funcstackframe *frame;
    __asm__("mov %%ebp, %0" : "=r"(frame));
    stacktrace_with_frame(frame);
}
