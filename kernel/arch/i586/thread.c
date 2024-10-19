#include "asm/contextswitch.h"
#include <kernel/arch/thread.h>
#include <kernel/io/tty.h>
#include <kernel/lib/diagnostics.h>
#include <kernel/lib/miscmath.h>
#include <kernel/mem/heap.h>
#include <stddef.h>
#include <stdint.h>

static bool const CONFIG_DEBUG_CONTEXT_SWITCH = false;

struct arch_thread {
    uintptr_t savedesp;
    int dummy[2];
    uint32_t stack[];
};

WARN_UNUSED_RESULT struct arch_thread *arch_thread_create(size_t minstacksize, uintptr_t entryaddr) {
    enum {
        STACK_IDX_EDI,
        STACK_IDX_ESI,
        STACK_IDX_EBX,
        STACK_IDX_EFLAGS,
        STACK_IDX_EBP,
        STACK_IDX_EIP,
        STACK_IDX_ARG1,
        STACK_IDX_ARG2,
        STACK_ITEM_COUNT
    };
    STATIC_ASSERT_TEST(STACK_ITEM_COUNT == 8);

    size_t stacksize = minstacksize;
    if ((stacksize % sizeof(uint32_t)) != 0) {
        stacksize = alignup(stacksize, sizeof(uint32_t));
    }
    assert((stacksize % sizeof(uint32_t)) == 0);
    tty_printf("creating thread with %uk stack and entry point %#lx\n", stacksize/1024, entryaddr);
    struct arch_thread *thread = NULL;
    if ((SIZE_MAX - sizeof(struct arch_thread)) < + stacksize) {
        goto out;
    }
    thread = heap_alloc(sizeof(*thread) + stacksize, 0);
    if (thread == NULL) {
        goto out;
    }
    size_t stack_top = stacksize / sizeof(uint32_t);
    uint32_t *esp = &thread->stack[stack_top - STACK_ITEM_COUNT];
    esp[STACK_IDX_ARG2]   = 0;                // Not used, but just in case someone expects a value to be there.
    esp[STACK_IDX_ARG1]   = 1;                // Not used, but just in case someone expects a value to be there.
    esp[STACK_IDX_EIP]    = entryaddr;
    esp[STACK_IDX_EBP]    = 0;
    esp[STACK_IDX_EFLAGS] = 0;
    esp[STACK_IDX_EBX]    = 0;
    esp[STACK_IDX_ESI]    = 0;
    esp[STACK_IDX_EDI]    = 0;
    thread->savedesp = (uintptr_t)esp;
out:
    return thread;
}

void arch_thread_destroy(struct arch_thread *thread) {
    heap_free(thread);
}

void arch_thread_switch(struct arch_thread *from, struct arch_thread *to) {
    if (CONFIG_DEBUG_CONTEXT_SWITCH) {
        tty_printf("context switch from=%p, to=%p(esp=%p)\n", from, to, to->savedesp);
    }
    if (from == NULL) {
        uintptr_t dummy_esp;
        archi586_contextswitch(&dummy_esp, to->savedesp);
    } else {
        archi586_contextswitch(&from->savedesp, to->savedesp);
    }
}
