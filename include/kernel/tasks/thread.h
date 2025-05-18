#pragma once
#include <kernel/lib/diagnostics.h>
#include <kernel/lib/list.h>
#include <kernel/tasks/mutex.h>
#include <kernel/tasks/sched.h>
#include <stddef.h>
#include <stdint.h>

#define THREAD_STACK_SIZE (1024 * 16)

struct Thread {
    /*
     * NOTE: The parent list depends on the context.
     * (e.g. Queued -> Queue's list, Waiting for mutex unlock -> Mutex wait
     * list)
     */
    struct List_Node sched_listnode;
    struct Arch_Thread *arch_thread;
    struct Mutex *waitingmutex;
    struct SourceLocation desired_locksource;
    int8_t priority;
    bool shutdown : 1;
};

/*
 * Those init_ parameters are only valid for initial setup.
 * This of course applies to any new thread, but the boot thread is exception: It's thread for already running code.
 *
 * Returns NULL if there's not enough memory.
 */
[[nodiscard]] struct Thread *Thread_Create(size_t init_stacksize, void (*init_mainfunc)(void *), void *init_data);
void Thread_Delete(struct Thread *thread);
void Thread_Switch(struct Thread *from, struct Thread *to);
