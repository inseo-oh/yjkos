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
    Co_Printf("stack trace:\n");
    struct funcstackframe *frame = startingframe;
    while (frame != NULL) {
        PHYSPTR physaddr;
        int ret = Arch_Mmu_VirtToPhys(&physaddr, frame);
        if (ret < 0) {
            Co_Printf("  stackframe at %p inaccessible(error %d) - STOP.\n", ret, frame);
            break;
        }
        Co_Printf("  %#lx\n", frame->eip);
        frame = frame->next;
    }
}

void Arch_StacktraceForTrapframe(void *trapframe) {
    if (trapframe == NULL) {
        Co_Printf("stack trace:\n  <no trace info available>\n");
        return;
    }
    Co_Printf("pc: %#lx\n", ((struct TrapFrame *)trapframe)->eip);
    stacktrace_with_frame((void *)((struct TrapFrame *)trapframe)->ebp);
}

void Arch_Stacktrace(void) {
    struct funcstackframe *frame;
    __asm__("mov %%ebp, %0" : "=r"(frame));
    stacktrace_with_frame(frame);
}
