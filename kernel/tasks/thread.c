#include <kernel/arch/thread.h>
#include <kernel/lib/strutil.h>
#include <kernel/mem/heap.h>
#include <kernel/tasks/thread.h>
#include <stddef.h>

struct thread *thread_create(size_t stacksize, void (*init_mainfunc)(void *), void *init_data) {
    struct thread *thread = heap_alloc(sizeof(*thread), HEAP_FLAG_ZEROMEMORY);
    if (thread == nullptr) {
        return nullptr;
    }
    thread->arch_thread = arch_thread_create(stacksize, init_mainfunc, init_data);
    if (thread->arch_thread == nullptr) {
        goto fail_arch_thread;
    }
    goto out;
fail_arch_thread:
    if (thread != nullptr) {
        arch_thread_destroy(thread->arch_thread);
        heap_free(thread);
        thread = nullptr;
    }
out:
    return thread;
}

void thread_delete(struct thread *thread) {
    if (thread == nullptr) {
        return;
    }
    arch_thread_destroy(thread->arch_thread);
    heap_free(thread);
}

void thread_switch(struct thread *from, struct thread *to) {
    arch_thread_switch(from->arch_thread, to->arch_thread);
}
