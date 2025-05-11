#pragma once
#include <kernel/lib/diagnostics.h>
#include <kernel/lib/list.h>
#include <kernel/tasks/mutex.h>
#include <kernel/tasks/sched.h>
#include <stddef.h>
#include <stdint.h>

#define THREAD_STACK_SIZE (1024 * 16)

struct thread {
    /*
     * NOTE: The parent list depends on the context.
     * (e.g. Queued -> Queue's list, Waiting for mutex unlock -> Mutex wait
     * list)
     */
    struct list_node sched_listnode;
    struct arch_thread *arch_thread;
    struct mutex *waitingmutex;
    struct sourcelocation desired_locksource;
    int8_t priority;
    bool shutdown : 1;
};

/*
 * Those init_ parameters are only valid for initial setup. This of course
 * applies to any new thread, but the boot thread is exception: It's thread for
 * already running code.
 *
 * Returns NULL if there's not enough memory.
 */
NODISCARD struct thread *thread_create(size_t init_stacksize, void (*init_mainfunc)(void *), void *init_data);
void thread_delete(struct thread *thread);
void thread_switch(struct thread *from, struct thread *to);
