#include <kernel/arch/thread.h>
#include <kernel/mem/heap.h>
#include <kernel/tasks/thread.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

struct Thread *Thread_Create(size_t stacksize, void (*init_mainfunc)(void *), void *init_data) {
    struct Thread *thread = Heap_Alloc(sizeof(*thread), HEAP_FLAG_ZEROMEMORY);
    if (thread == NULL) {
        return NULL;
    }
    thread->arch_thread = Arch_Thread_Create(stacksize, init_mainfunc, init_data);
    if (thread->arch_thread == NULL) {
        goto fail_arch_thread;
    }
    goto out;
fail_arch_thread:
    if (thread != NULL) {
        Arch_Thread_Destroy(thread->arch_thread);
        Heap_Free(thread);
        thread = NULL;
    }
out:
    return thread;
}

void Thread_Delete(struct Thread *thread) {
    if (thread == NULL) {
        return;
    }
    Arch_Thread_Destroy(thread->arch_thread);
    Heap_Free(thread);
}

void Thread_Switch(struct Thread *from, struct Thread *to) {
    Arch_Thread_Switch(from->arch_thread, to->arch_thread);
}
