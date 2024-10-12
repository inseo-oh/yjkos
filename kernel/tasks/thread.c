#include <kernel/arch/thread.h>
#include <kernel/mem/heap.h>
#include <kernel/status.h>
#include <kernel/tasks/thread.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

FAILABLE_FUNCTION thread_create(struct thread **thread_out, size_t minstacksize, uintptr_t entryaddr) {
FAILABLE_PROLOGUE
    struct thread *thread = heap_alloc(sizeof(*thread), HEAP_FLAG_ZEROMEMORY);
    if (thread == NULL) {
        THROW(ERR_NOMEM);
    }
    TRY(arch_thread_create(&thread->arch_thread, minstacksize, entryaddr));
    *thread_out = thread;
FAILABLE_EPILOGUE_BEGIN
    if (DID_FAIL) {
        if (thread != NULL) {
            arch_thread_destroy(thread->arch_thread);
            heap_free(thread);
        }
    }
FAILABLE_EPILOGUE_END
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
