#pragma once
#include <kernel/tasks/sched.h>
#include <kernel/lib/list.h>
#include <kernel/status.h>
#include <stdint.h>

struct thread {
    struct list_node sched_queuelistnode;
    int8_t priority;
    struct arch_thread *arch_thread;
};

FAILABLE_FUNCTION thread_create(struct thread **thread_out, size_t minstacksize, uintptr_t entryaddr);
void thread_delete(struct thread *thread);
void thread_switch(struct thread *from, struct thread *to);
