#include <assert.h>
#include <errno.h>
#include <kernel/arch/interrupts.h>
#include <kernel/io/co.h>
#include <kernel/lib/diagnostics.h>
#include <kernel/lib/list.h>
#include <kernel/mem/heap.h>
#include <kernel/panic.h>
#include <kernel/tasks/mutex.h>
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
static struct sched_queue *s_currentqueuenode;
static struct thread *s_runningthread;
static struct list s_mutexwaitthreads;

static WARN_UNUSED_RESULT struct sched_queue *getqueue(int8_t priority) {
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
        LIST_FOREACH(&s_queues, queuenode) {
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
            if ((queue->priority < priority) &&
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
                list_insertafter(
                    &s_queues, insertafter,
                    &resultqueue->node, resultqueue);
            }
        }
    }
    return resultqueue;
}

static struct sched_queue *picknextqueue(void) {
    struct list_node *node = NULL;
    if (s_currentqueuenode != NULL) {
        node = &s_currentqueuenode->node;
    }
    for (size_t i = 0; i < 2; i++) {
        for (; node != NULL; node = node->next) {
            if ((s_currentqueuenode != NULL) &&
                (&s_currentqueuenode->node == node))
            {
                continue;
            }
            struct sched_queue *queue = node->data;
            assert(queue);
            if ((queue->opportunities != 0) && (queue->threads.front != NULL)) {
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
        LIST_FOREACH(&s_queues, queuenode) {
            struct sched_queue *queue = queuenode->data;
            assert(queue);
            queue->opportunities = opportunities;
            opportunities++;
        }
    }
    struct sched_queue *queue = node->data;
    assert(queue);
    queue->opportunities--;
    s_currentqueuenode = queue;
    return queue;
}

static struct thread *picknexttask(void) {
    bool previnterrupts = arch_interrupts_disable();
    struct thread *result = NULL;

    while (1) {
        LIST_FOREACH(&s_mutexwaitthreads, threadnode) {
            struct thread *thread = threadnode->data;
            assert(thread->waitingmutex != NULL);
            if (__mutex_trylock(
                thread->waitingmutex,
                thread->desired_locksource.filename,
                thread->desired_locksource.func,
                thread->desired_locksource.line))
            {
                list_removenode(
                    &s_mutexwaitthreads, &thread->sched_listnode);
                result = thread;
                if (result->shutdown) {
                    co_printf(
                        "sched: thread is about to shutdown - unlocking mutex\n");
                    mutex_unlock(thread->waitingmutex);
                }
                thread->waitingmutex = NULL;
                /*
                 * Since the list was mutated inside loop, we must get out of 
                 * here
                 */
                break;
            }
        } 
        if (result == NULL) do {
            struct sched_queue *queue = picknextqueue();
            if (queue == NULL) {
                break;
            }
            struct list_node *node = list_removeback(&queue->threads);
            if (node == NULL) {
                break;
            }
            result = node->data;
        } while(0);
        if ((result == NULL) || (!result->shutdown)) {
            break;
        }
        co_printf("sched: shutting down thread %p\n", result);
        thread_delete(result);
        result = NULL;
    }
    interrupts_restore(previnterrupts);
    return result;
}

void sched_printqueues(void) {
    co_printf("----- QUEUE LIST -----\n");
    LIST_FOREACH(&s_queues, queuenode) {
        struct sched_queue *queue = queuenode->data;
        co_printf(
            "queue %p - Pri %d [threads exist: %d]\n",
            queue, queue->priority, queue->threads.front != NULL);
        LIST_FOREACH(&queue->threads, threadnode) {
            co_printf(" - thread %p\n", threadnode);
        }
    }
}

void sched_waitmutex(
    struct mutex *mutex, struct mutex_locksource const *locksource)
{
    assert(mutex->locked);
    bool previnterrupts = arch_interrupts_disable();
    memcpy(
        &s_runningthread->desired_locksource, locksource,
        sizeof(*locksource));
    struct thread *nextthread = picknexttask();
    if (nextthread == NULL) {
        co_printf(
            "sched: WARNING: there is no thread to wait for mutex\n");
        co_printf(
            "mutex is currently locked by %s:%d (%s)\n",
            mutex->locksource.filename, mutex->locksource.line,
            mutex->locksource.func);
        co_printf(
            "lock requested by %s:%d (%s)\n",
            locksource->filename, locksource->line,
            locksource->func);
        sched_printqueues();
        while(1);
        goto out;
    }
    assert(s_runningthread != NULL);
    assert(s_runningthread->waitingmutex == NULL);
    s_runningthread->waitingmutex = mutex;
    list_insertfront(
        &s_mutexwaitthreads, &s_runningthread->sched_listnode,
        s_runningthread);
    struct thread *oldthread = s_runningthread;
    assert(nextthread != oldthread);
    s_runningthread = nextthread;
    thread_switch(oldthread, nextthread);
out:
    interrupts_restore(previnterrupts);
}

WARN_UNUSED_RESULT int sched_queue(struct thread *thread) {
    int ret = 0;
    bool previnterrupts = arch_interrupts_disable();
    struct sched_queue *queue = getqueue(thread->priority);
    if (queue == NULL) {
        ret = -ENOMEM;
        goto out;
    }
    list_insertfront(
        &queue->threads, &thread->sched_listnode,
        thread);
    goto out;
out:
    interrupts_restore(previnterrupts);
    return ret;
}

void sched_schedule(void) {
    bool previnterrupts = arch_interrupts_disable();
    struct thread *nextthread = picknexttask();
    if (nextthread == NULL) {
        goto out;
    }
    /* 
     * Note that it is safe to call sched_schedule() before calling
     * sched_initbootthread(), as long as it's called before creating
     * any other threads.
     * If there are no other threads to switch, we will never reach here,
     * so it doesn't trip below assertion.
     */
    assert(s_runningthread != NULL);
    int ret = sched_queue(s_runningthread);
    if (ret < 0) {
        co_printf(
            "sched: failed to queue current thread(error %d)\n",
            ret);
    }
    struct thread *oldthread = s_runningthread;
    assert(oldthread != NULL);
    assert(nextthread != oldthread);
    s_runningthread = nextthread;
    thread_switch(oldthread, nextthread);
out:
    interrupts_restore(previnterrupts);
}

void sched_initbootthread(void) {
    assert(s_runningthread == NULL);
    /*
     * Init values for thread is not used, as those are only used for fresh new 
     * threads, and boot thread is what we are running now.
     */
    s_runningthread = thread_create(
        0, NULL, NULL);
    assert(s_runningthread != NULL);
    s_runningthread->priority = 20;
}
