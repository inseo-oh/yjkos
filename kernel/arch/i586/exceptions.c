#include "exceptions.h"
#include "asm/i586.h"
#include <kernel/arch/hcf.h>
#include <kernel/arch/stacktrace.h>
#include <kernel/io/co.h>
#include <kernel/mem/vmm.h>
#include <kernel/trapmanager.h>
#include <stddef.h>
#include <stdint.h>

static void dump_trapframe(struct TrapFrame *self) {
    Co_Printf(
        "eax=%08lx ebx=%08lx ecx=%08lx edx=%08lx esi=%08lx edi=%08lx\n"
        "ebp=%08lx eip=%08lx efl=%08lx cs =%08lx ds =%08lx es =%08lx\n"
        "fs =%08lx gs =%08lx\n",
        self->eax, self->ebx, self->ecx, self->edx, self->esi, self->edi,
        self->ebp, self->eip, self->eflags, self->cs, self->ds, self->es,
        self->fs, self->gs);
    Arch_StacktraceForTrapframe(self);
}

static void defaulthandler(int trapnum, void *trapframe, void *data) {
    (void)data;

    struct TrapFrame *frame = trapframe;
    Co_Printf("fatal exception %d occured (error code %#x)\n", trapnum, frame->errcode);
    dump_trapframe(frame);
    Arch_Hcf();
}

#define PF_FLAG_P (1U << 0) /* Present */
#define PF_FLAG_W (1U << 1) /* write */
#define PF_FLAG_U (1U << 2) /* User */

static void pagefaulthandler(int trapnum, void *trapframe, void *data) {
    (void)data;
    (void)trapnum;
    struct TrapFrame *frame = trapframe;
    void *faultaddr = ArchI586_ReadCr2();
    Vmm_PageFault(faultaddr, frame->errcode & PF_FLAG_P, frame->errcode & PF_FLAG_W, frame->errcode & PF_FLAG_U, frame);
}

static struct TrapHandler s_traphandler[32];

void ArchI586_Exceptions_Init(void) {
    for (int i = 0; i < 32; i++) {
        switch (i) {
        case 14:
            TrapManager_Register(&s_traphandler[i], i, pagefaulthandler, NULL);
            break;
        default:
            TrapManager_Register(&s_traphandler[i], i, defaulthandler, NULL);
        }
    }
}
