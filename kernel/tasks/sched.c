#include "testimage.h"
#include <assert.h>
#include <kernel/arch/interrupts.h>
#include <kernel/io/tty.h>
#include <kernel/lib/diagnostics.h>
#include <kernel/lib/list.h>
#include <kernel/mem/heap.h>
#include <kernel/tasks/sched.h>
#include <kernel/tasks/thread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// Each queue has associated priority, and when selecting the next thread we start from
// lowest priority, but every time a queue is selected, remaining opportunities is decreased.
// (Initial opportunities count is 1 for lowest priority, 2 for next lowest, and so on...)
//
// When opportunities become zero, that queue is no longer selected, thus lower priority queues
// are selected less than higher priority ones.
static struct list s_queues;
static struct list_node *s_currentqueuenode;
static struct thread *s_runningthread;

WARN_UNUSED_RESULT struct sched_queue *sched_getqueue(int8_t priority) {
    struct list_node *insertafter = NULL;
    struct sched_queue *resultqueue = NULL;
    bool shouldinsertfront = false;
    if (s_queues.front != NULL) {
        struct list_node *queuenode = s_queues.front;
        struct sched_queue *queue = queuenode->data;
        assert(queue);
        if (priority < queue->priority) {
            shouldinsertfront = true;
        }
    }
    if (!shouldinsertfront) {
        for (struct list_node *queuenode = s_queues.front; queuenode != NULL; queuenode = queuenode->next) {
            struct sched_queue *queue = queuenode->data;
            assert(queue);
            struct list_node *nextququenode = queuenode->next;
            // Found the queue
            if (queue->priority == priority) {
                resultqueue = queue;
                break;
            }
            // No more queue
            if (nextququenode == NULL) {
                insertafter = queuenode;
                break;
            }
            struct sched_queue *nextqueue = nextququenode->data;
            assert(nextqueue);
            // Given priority is between current and next queue's priority.
            if ((queue->priority < priority) && (priority < nextqueue->priority)) {
                insertafter = queuenode;
                break;
            }
        }
    }
    if (resultqueue == NULL) {
        resultqueue = heap_alloc(sizeof(*resultqueue), HEAP_FLAG_ZEROMEMORY);
        if (resultqueue != NULL) {
            resultqueue->priority = priority;
            if (insertafter == NULL) {
                list_insertfront(&s_queues, &resultqueue->node, resultqueue);
            } else {
                list_insertafter(&s_queues, insertafter, &resultqueue->node, resultqueue);
            }
        }
    }
    return resultqueue;
}

struct sched_queue *sched_picknextqueue(void) {
    struct list_node *node = s_currentqueuenode;
    for (size_t i = 0; i < 2; i++) {
        for (; node != NULL; node = node->next) {
            if (s_currentqueuenode == node) {
                continue;
            }
            struct sched_queue *queue = node->data;
            assert(queue);
            if (queue->opportunities != 0) {
                break;
            }
        }
        // If we couldn't find any queues, reset to beginning and try again.
        if ((node == NULL) && (i == 0)) {
            node = s_queues.front;
        }
    }

    if (node == NULL) {
        // We have to reset the scheduler either because we are scheduling for the first time,
        // or every queue ran out of opportunities.
        node = s_queues.front;
        if (node == NULL) {
            return NULL;
        }
        // Reset opportunities.
        size_t opportunities = 1;
        for (struct list_node *node = s_queues.back; node != NULL; node = node->prev, opportunities++) {
            struct sched_queue *queue = node->data;
            assert(queue);
            queue->opportunities = opportunities;
        }
    }
    struct sched_queue *queue = node->data;
    assert(queue);
    queue->opportunities--;
    s_currentqueuenode = &queue->node;
    return queue;
}

void sched_printqueues(void) {
    tty_printf("----- QUEUE LIST -----\n");
    for (struct list_node *queuenode = s_queues.front; queuenode != NULL; queuenode = queuenode->next) {
        struct sched_queue *queue = queuenode->data;
        tty_printf("queue %p - Pri %d [threads exist: %d]\n", queue, queue->priority, queue->threads.front != NULL);
        for (struct list_node *threadnode = queue->threads.front; threadnode != NULL; threadnode = threadnode->next) {
            tty_printf(" - thread %p\n", threadnode);
        }
    }
}

// Returns false if there's not enough memory.
WARN_UNUSED_RESULT bool sched_queue(struct thread *thread) {
    struct sched_queue *queue =sched_getqueue(thread->priority);
    if (queue == NULL) {
        return false;
    }
    list_insertfront(&queue->threads, &thread->sched_queuelistnode, thread);
    return true;
}

void sched_schedule(void) {
    struct sched_queue *queue = sched_picknextqueue();
    if (queue == NULL) {
        return;
    }
    struct list_node *nextthreadnode = list_removeback(&queue->threads);
    if (nextthreadnode == NULL) {
        return;
    }
    if (s_runningthread != NULL) {
        if (!sched_queue(s_runningthread)) {
            tty_printf(
                "sched: not enough memory to queue current thread\n");
        }
    }
    struct thread *nextthread = nextthreadnode->data;
    struct thread *oldthread = s_runningthread;
    assert(nextthread != oldthread);
    s_runningthread = nextthread;
    thread_switch(oldthread, nextthread);
}


////////////////////////////////////////////////////////////////////////////////

static struct thread *thread1;
static struct thread *thread2;
static struct thread *thread3;

static char s_videobuf[25][80];

#if 0
static void task1(void) {
    arch_interrupts_enable();
    size_t counter = 0;
    uint32_t rnd1, rnd2;
    uint32_t rnd1_org, rnd2_org;
    tty_printf("entered TASK1\n");
    while(1) {
        rnd1_org = rand();
        rnd2_org = rand();
        if (counter % 10000 == 0) {
            tty_printf("TASK1 local counter: %u\n", counter);
        }
        counter++;
        rnd1 = rnd1_org;
        rnd2 = rnd2_org;
        assert(rnd1 == rnd1_org);
        assert(rnd2 == rnd2_org);
    }
}

static void task2(void) {
    arch_interrupts_enable();
    size_t counter = 0;
    tty_printf("entered TASK2\n");
    uint64_t rnd1, rnd2;
    uint64_t rnd1_org, rnd2_org;
    while(1) {
        rnd1_org = rand();
        rnd2_org = rand();
        if (counter % 10000 == 0) {
            tty_printf("TASK2 local counter: %u\n", counter);
        }
        counter++;
        rnd1 = rnd1_org;
        rnd2 = rnd2_org;
        assert(rnd1 == rnd1_org);
        assert(rnd2 == rnd2_org);
    }
}
#else
static void task1(void) {
    arch_interrupts_enable();
    int srclineindex = 0;
    while(1) {
        assert(srclineindex % 25 == 0);
        for (int destlineindex = 0; destlineindex < 25; destlineindex++) {
            memcpy(s_videobuf[destlineindex], VIDEO_LINES[srclineindex], 80);
            srclineindex++;
            srclineindex %= VIDEO_LINE_COUNT;
        }
    }
}

static void task2(void) {
    arch_interrupts_enable();
    while(1) {
        tty_printf("\a");
        for (int srclineindex = 0; srclineindex < 25; srclineindex++) {
            char buf[81];
            buf[80] = '\0';
            memcpy(buf, s_videobuf[srclineindex], 80);
            tty_printf("%s", buf);
        }
    }
}

static void task3(void) {
    arch_interrupts_enable();
    while(1) {
        for (int srclineindex = 0; srclineindex < 25; srclineindex++) {
            char buf[81];
            buf[80] = '\0';
            memcpy(buf, s_videobuf[srclineindex], 80);
        }
    }
}
#endif

void sched_test(void) {
    static size_t const STACK_SIZE = 65536;
    bool disabled = arch_interrupts_disable();
    thread1 = thread_create(STACK_SIZE, (uintptr_t)task1);
    assert(thread1 != NULL);
    thread2 = thread_create(STACK_SIZE, (uintptr_t)task2);
    assert(thread2 != NULL);
    thread3 = thread_create(STACK_SIZE, (uintptr_t)task3);
    assert(thread3 != NULL);
    bool ok = sched_queue(thread1);
    assert(ok);
    sched_printqueues();
    ok = sched_queue(thread2);
    assert(ok);
    ok = sched_queue(thread3);
    assert(ok);
    thread2->priority = -20;
    sched_printqueues();
    if (disabled) {
        arch_interrupts_enable();
    }
}
