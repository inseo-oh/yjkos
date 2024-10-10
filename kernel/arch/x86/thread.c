#include "asm/contextswitch.h"
#include <kernel/arch/thread.h>
#include <kernel/io/tty.h>
#include <kernel/lib/diagnostics.h>
#include <kernel/lib/miscmath.h>
#include <kernel/mem/heap.h>
#include <kernel/status.h>
#include <stddef.h>
#include <stdint.h>

static bool const CONFIG_DEBUG_CONTEXT_SWITCH = false;

struct arch_thread {
    uintptr_t savedesp;
    int dummy[2];
    uint32_t stack[];
};

FAILABLE_FUNCTION arch_thread_create(arch_thread_t **thread_out, size_t minstacksize, uintptr_t entryaddr) {
FAILABLE_PROLOGUE
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
    if ((SIZE_MAX - sizeof(arch_thread_t)) < + stacksize) {
        THROW(ERR_NOMEM);
    }
    arch_thread_t *thread = heap_alloc(sizeof(*thread) + stacksize, 0);
    if (thread == NULL) {
        THROW(ERR_NOMEM);
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
    *thread_out = thread;
FAILABLE_EPILOGUE_BEGIN
FAILABLE_EPILOGUE_END
}

void arch_thread_destroy(arch_thread_t *thread) {
    heap_free(thread);
}

void arch_thread_switch(arch_thread_t *from, arch_thread_t *to) {
    if (CONFIG_DEBUG_CONTEXT_SWITCH) {
        tty_printf("context switch from=%p, to=%p(esp=%p)\n", from, to, to->savedesp);
    }
    if (from == NULL) {
        uintptr_t dummy_esp;
        archx86_contextswitch(&dummy_esp, to->savedesp);
    } else {
        archx86_contextswitch(&from->savedesp, to->savedesp);
    }
}
