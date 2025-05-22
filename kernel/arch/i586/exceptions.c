#include "exceptions.h"
#include "asm/i586.h"
#include <kernel/arch/hcf.h>
#include <kernel/arch/stacktrace.h>
#include <kernel/io/co.h>
#include <kernel/mem/vmm.h>
#include <kernel/trapmanager.h>
#include <stdint.h>

static void dump_trapframe(struct trap_frame *self) {
    co_printf(
        "eax=%08lx ebx=%08lx ecx=%08lx edx=%08lx esi=%08lx edi=%08lx\n"
        "ebp=%08lx eip=%08lx efl=%08lx cs =%08lx ds =%08lx es =%08lx\n"
        "fs =%08lx gs =%08lx\n",
        self->eax, self->ebx, self->ecx, self->edx, self->esi, self->edi,
        self->ebp, self->eip, self->eflags, self->cs, self->ds, self->es,
        self->fs, self->gs);
    arch_stacktrace_for_trapframe(self);
}

static void defaulthandler(int trapnum, void *trapframe, void *data) {
    (void)data;

    struct trap_frame *frame = trapframe;
    co_printf("fatal exception %d occured (error code %#x)\n", trapnum, frame->errcode);
    dump_trapframe(frame);
    arch_hcf();
}

#define PF_FLAG_P (1U << 0) /* Present */
#define PF_FLAG_W (1U << 1) /* write */
#define PF_FLAG_U (1U << 2) /* User */

static void pagefaulthandler(int trapnum, void *trapframe, void *data) {
    (void)data;
    (void)trapnum;
    struct trap_frame *frame = trapframe;
    void *faultaddr = archi586_read_cr2();
    vmm_page_fault(faultaddr, frame->errcode & PF_FLAG_P, frame->errcode & PF_FLAG_W, frame->errcode & PF_FLAG_U, frame);
}

static struct trap_handler s_traphandler[32];

void archi586_exceptions_init(void) {
    for (int i = 0; i < 32; i++) {
        switch (i) {
        case 14:
            trapmanager_register_trap(&s_traphandler[i], i, pagefaulthandler, nullptr);
            break;
        default:
            trapmanager_register_trap(&s_traphandler[i], i, defaulthandler, nullptr);
        }
    }
}
