#pragma once
#include <kernel/lib/list.h>
#include <kernel/status.h>
#include <stddef.h>
#include <stdint.h>

typedef int8_t sched_priority_t;
typedef struct thread thread_t;

typedef struct sched_queue sched_queue_t;
struct sched_queue {
    list_node_t node;
    // Lower value -> Higher priority (Works similiarly to UNIX niceness value)
    sched_priority_t priority;
    size_t opportunities;
    list_t threads;
};

// NOTE: Most scheduler functions are considered as critical section, so it is safe to turn off interrupts
//       before using any of these!
//       TODO: Just do that in sched itself :D

FAILABLE_FUNCTION sched_getqueue(sched_queue_t **queue_out, sched_priority_t priority);
sched_queue_t *sched_picknextqueue(void);
void sched_printqueues(void);
FAILABLE_FUNCTION sched_queue(thread_t *thread);
void sched_schedule(void);

void sched_test(void);
