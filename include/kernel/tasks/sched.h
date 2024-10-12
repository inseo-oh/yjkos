#pragma once
#include <kernel/lib/list.h>
#include <kernel/status.h>
#include <stddef.h>
#include <stdint.h>

struct thread;

typedef int8_t sched_priority_t;

struct sched_queue {
    struct list_node node;
    // Lower value -> Higher priority (Works similiarly to UNIX niceness value)
    sched_priority_t priority;
    size_t opportunities;
    struct list threads;
};

// NOTE: Most scheduler functions are considered as critical section, so it is safe to turn off interrupts
//       before using any of these!
//       TODO: Just do that in sched itself :D

FAILABLE_FUNCTION sched_getqueue(struct sched_queue **queue_out, sched_priority_t priority);
struct sched_queue *sched_picknextqueue(void);
void sched_printqueues(void);
FAILABLE_FUNCTION sched_queue(struct thread *thread);
void sched_schedule(void);

void sched_test(void);
