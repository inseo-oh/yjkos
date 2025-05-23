#include "asm/contextswitch.h"
#include <kernel/arch/stacktrace.h>
#include <kernel/arch/thread.h>
#include <kernel/io/co.h>
#include <kernel/lib/diagnostics.h>
#include <kernel/lib/miscmath.h>
#include <kernel/mem/heap.h>
#include <stddef.h>
#include <stdint.h>

static bool const CONFIG_DEBUG_CONTEXT_SWITCH = false;

struct arch_thread {
    void *saved_esp;
    uint32_t stack[];
};

typedef enum {
    STACK_IDX_EDI,
    STACK_IDX_ESI,
    STACK_IDX_EBX,
    STACK_IDX_EFLAGS,
    STACK_IDX_EBP,
    STACK_IDX_EIP,
    STACK_IDX_MAIN_RETADDR,
    STACK_IDX_MAIN_ARG0,
    STACK_ITEM_COUNT
} STACK_IDX;

static void exitcallback(void) {
    /* tty_printf("thread done - going home\n"); */
    while (1) {
        ;
    }
}

#define STACK_MINSIZE STACK_ITEM_COUNT * sizeof(uint32_t)
[[nodiscard]] struct arch_thread *arch_thread_create(size_t init_stacksize, void (*init_mainfunc)(void *), void *init_data) {
    STATIC_ASSERT_TEST(STACK_ITEM_COUNT == 8);

    size_t stacksize = init_stacksize;
    if (stacksize < STACK_MINSIZE) {
        stacksize = STACK_MINSIZE;
    }
    if ((stacksize % sizeof(uint32_t)) != 0) {
        stacksize = align_up(stacksize, sizeof(uint32_t));
    }
    assert((stacksize % sizeof(uint32_t)) == 0);
    co_printf("creating thread with %uk stack and entry point %p\n", stacksize / 1024, init_mainfunc);
    struct arch_thread *thread = nullptr;

    if ((SIZE_MAX - sizeof(struct arch_thread)) < stacksize) {
        goto out;
    }
    thread = heap_alloc(sizeof(*thread) + stacksize, 0);
    if (thread == nullptr) {
        goto out;
    }
    size_t stack_top = stacksize / sizeof(uint32_t);
    uint32_t *esp = &thread->stack[stack_top - STACK_ITEM_COUNT];
    esp[STACK_IDX_MAIN_RETADDR] = (uintptr_t)exitcallback;
    esp[STACK_IDX_MAIN_ARG0] = (uintptr_t)init_data;
    esp[STACK_IDX_EIP] = (uintptr_t)init_mainfunc;
    esp[STACK_IDX_EBP] = 0;
    esp[STACK_IDX_EFLAGS] = 0;
    esp[STACK_IDX_EBX] = 0;
    esp[STACK_IDX_ESI] = 0;
    esp[STACK_IDX_EDI] = 0;
    thread->saved_esp = esp;
out:
    return thread;
}

void arch_thread_destroy(struct arch_thread *thread) {
    heap_free(thread);
}

void arch_thread_switch(struct arch_thread *from, struct arch_thread *to) {
    if (CONFIG_DEBUG_CONTEXT_SWITCH) {
        co_printf("context switch from=%p, to=%p(esp=%p)\n", from, to, to->saved_esp);
        arch_stacktrace();
        uint32_t edi = ((uint32_t *)to->saved_esp)[STACK_IDX_EDI];
        uint32_t esi = ((uint32_t *)to->saved_esp)[STACK_IDX_ESI];
        uint32_t ebx = ((uint32_t *)to->saved_esp)[STACK_IDX_EBX];
        uint32_t eflags = ((uint32_t *)to->saved_esp)[STACK_IDX_EFLAGS];
        uint32_t ebp = ((uint32_t *)to->saved_esp)[STACK_IDX_EBP];
        uint32_t eip = ((uint32_t *)to->saved_esp)[STACK_IDX_EIP];
        co_printf("ebx=%08lx esi=%08lx edi=%08lx\n", ebx, esi, edi);
        co_printf("ebp=%08lx eip=%08lx efl=%08lx\n", ebp, eip, eflags);
    }
    assert(from != nullptr);
    archi586_context_switch(&from->saved_esp, to->saved_esp);
    if (CONFIG_DEBUG_CONTEXT_SWITCH) {
        co_printf("context switch returned! from=%p(esp=%p), to=%p\n", from, from->saved_esp, to);
        arch_stacktrace();
        uint32_t edi = ((uint32_t *)from->saved_esp)[STACK_IDX_EDI];
        uint32_t esi = ((uint32_t *)from->saved_esp)[STACK_IDX_ESI];
        uint32_t ebx = ((uint32_t *)from->saved_esp)[STACK_IDX_EBX];
        uint32_t eflags = ((uint32_t *)to->saved_esp)[STACK_IDX_EFLAGS];
        uint32_t ebp = ((uint32_t *)from->saved_esp)[STACK_IDX_EBP];
        uint32_t eip = ((uint32_t *)from->saved_esp)[STACK_IDX_EIP];
        co_printf("ebx=%08lx esi=%08lx edi=%08lx\n", ebx, esi, edi);
        co_printf("ebp=%08lx eip=%08lx efl=%08lx\n", ebp, eip, eflags);
    }
}
