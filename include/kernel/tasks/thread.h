#pragma once
#include <kernel/arch/thread.h>
#include <kernel/tasks/sched.h>
#include <kernel/lib/list.h>
#include <kernel/status.h>
#include <stddef.h>
#include <stdint.h>

typedef struct thread thread_t;
struct thread {
    list_node_t sched_queuelistnode;
    sched_priority_t priority;
    arch_thread_t *arch_thread;
};

FAILABLE_FUNCTION thread_create(thread_t **thread_out, size_t minstacksize, uintptr_t entryaddr);
void thread_delete(thread_t *thread);
void thread_switch(thread_t *from, thread_t *to);
