#pragma once
#include <kernel/lib/diagnostics.h>
#include <kernel/lib/list.h>
#include <kernel/tasks/mutex.h>
#include <stddef.h>
#include <stdint.h>

struct Thread;

struct Sched_Queue {
    struct List_Node node;
    /* Lower value -> Higher priority (Works similiarly to UNIX niceness value) */
    int8_t priority;
    size_t opportunities;
    struct List threads;
};

/*
 * NOTE: Most scheduler functions are considered as critical section, so it is safe to turn off interrupts before using any of these!
 *       TODO: Just do that in sched itself :D
 */

/*
 * Returns NULL if sched needs to create a new queue and there's not enough
 * memory.
 */
void Sched_PrintQueues(void);
void Sched_WaitMutex(struct Mutex *mutex, struct SourceLocation const *locksource);
[[nodiscard]] int Sched_Queue(struct Thread *thread);
void Sched_Schedule(void);
void Sched_InitBootThread(void);
