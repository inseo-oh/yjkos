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

/*
 * Each queue has associated priority, and when selecting the next thread we 
 * start from lowest priority, but every time a queue is selected, remaining 
 * opportunities is decreased.
 * (Initial opportunities count is 1 for lowest priority, 2 for next lowest, 
 * and so on...)
 *
 * When opportunities become zero, that queue is no longer selected, thus lower 
 * priority queues are selected less than higher priority ones.
 */
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
        for (
            struct list_node *queuenode = s_queues.front; queuenode != NULL;
            queuenode = queuenode->next)
        {
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
            if (
                (queue->priority < priority) &&
                (priority < nextqueue->priority))
            {
                insertafter = queuenode;
                break;
            }
        }
    }
    if (resultqueue == NULL) {
        resultqueue = heap_alloc(
            sizeof(*resultqueue), HEAP_FLAG_ZEROMEMORY);
        if (resultqueue != NULL) {
            resultqueue->priority = priority;
            if (insertafter == NULL) {
                list_insertfront(
                    &s_queues, &resultqueue->node,
                    resultqueue);
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
        /*
         * We have to reset the scheduler either because we are scheduling for
         * the first time, or every queue ran out of opportunities.
         */
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
