#pragma once
#include <kernel/lib/diagnostics.h>
#include <kernel/lib/list.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

struct thread;

struct sched_queue {
    struct list_node node;
    // Lower value -> Higher priority (Works similiarly to UNIX niceness value)
    int8_t priority;
    size_t opportunities;
    struct list threads;
};

// NOTE: Most scheduler functions are considered as critical section, so it is safe to turn off interrupts
//       before using any of these!
//       TODO: Just do that in sched itself :D

/*
 * Returns NULL if sched needs to create a new queue and there's not enough
 * memory.
 */
WARN_UNUSED_RESULT struct sched_queue *sched_getqueue(int8_t priority);
struct sched_queue *sched_picknextqueue(void);
void sched_printqueues(void);
/*
 * Returns false if sched needs to create a new queue and there's not enough
 * memory.
 */
WARN_UNUSED_RESULT bool sched_queue(struct thread *thread);
void sched_schedule(void);

