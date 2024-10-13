#include <kernel/arch/thread.h>
#include <kernel/mem/heap.h>
#include <kernel/tasks/thread.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

struct thread *thread_create(size_t minstacksize, uintptr_t entryaddr) {
    struct thread *thread = heap_alloc(sizeof(*thread), HEAP_FLAG_ZEROMEMORY);
    if (thread == NULL) {
        return NULL;
    }
    thread->arch_thread = arch_thread_create(minstacksize, entryaddr);
    if (thread->arch_thread == NULL) {
        goto fail_arch_thread;
    }
    goto out;
fail_arch_thread:
    if (thread != NULL) {
        arch_thread_destroy(thread->arch_thread);
        heap_free(thread);
        thread = NULL;
    }
out:
    return thread;
}

void thread_delete(struct thread *thread) {
    if (thread == NULL) {
        return;
    }
    arch_thread_destroy(thread->arch_thread);
    heap_free(thread);
}

// `from` may be NULL if task switching is done for the first time.
void thread_switch(struct thread *from, struct thread *to) {
    if (from == NULL) {
        arch_thread_switch(NULL, to->arch_thread);
        return;
    }
    arch_thread_switch(from->arch_thread, to->arch_thread);
}
