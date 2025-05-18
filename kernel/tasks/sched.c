#include <assert.h>
#include <errno.h>
#include <kernel/arch/interrupts.h>
#include <kernel/io/co.h>
#include <kernel/lib/diagnostics.h>
#include <kernel/lib/list.h>
#include <kernel/mem/heap.h>
#include <kernel/tasks/mutex.h>
#include <kernel/tasks/sched.h>
#include <kernel/tasks/thread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define BOOT_THREAD_PRIORITY 20

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
static struct List s_queues;
static struct Sched_Queue *s_current_queue_node;
static struct Thread *s_runningthread;
static struct List s_mutexwaitthreads;

/*
 * If insert_after is NULL, queue will be inserted at front.
 */
[[nodiscard]] static struct Sched_Queue *alloc_queue(struct Sched_Queue *insert_after, int8_t priority) {
    struct Sched_Queue *resultqueue = Heap_Alloc(sizeof(*resultqueue), HEAP_FLAG_ZEROMEMORY);
    if (resultqueue == NULL) {
        return NULL;
    }
    resultqueue->priority = priority;
    if (insert_after == NULL) {
        List_InsertFront(&s_queues, &resultqueue->node, resultqueue);
    } else {
        List_InsertAfter(&s_queues, &insert_after->node, &resultqueue->node, resultqueue);
    }
    return resultqueue;
}

[[nodiscard]] static struct Sched_Queue *get_queue(int8_t priority) {
    struct List_Node *insert_after = NULL;
    struct Sched_Queue *result_queue = NULL;
    bool should_insert_front = false;
    struct Sched_Queue *queue = List_GetDataOrNull(s_queues.front);
    if (queue != NULL) {
        if (priority < queue->priority) {
            should_insert_front = true;
        }
    }
    if (!should_insert_front) {
        LIST_FOREACH(&s_queues, queue_node) {
            struct Sched_Queue *queue = queue_node->data;
            assert(queue);
            struct Sched_Queue *next_queue = List_GetDataOrNull(queue_node->next);
            /* Found the queue */
            if (queue->priority == priority) {
                result_queue = queue;
                break;
            }
            /* No more queue */
            if (next_queue == NULL) {
                insert_after = queue_node;
                break;
            }
            /* Given priority is between current and next queue's priority. */
            if ((queue->priority < priority) &&
                (priority < next_queue->priority)) {
                insert_after = queue_node;
                break;
            }
        }
    }
    if (result_queue != NULL) {
        return result_queue;
    }
    return alloc_queue(List_GetDataOrNull(insert_after), priority);
}

static void reset_queues(void) {
    /*
     * We have to reset the scheduler either because we are scheduling for
     * the first time, or every queue ran out of opportunities.
     */

    /* Reset opportunities. */
    size_t opportunities = 1;
    LIST_FOREACH(&s_queues, queuenode) {
        struct Sched_Queue *queue = queuenode->data;
        assert(queue);
        queue->opportunities = opportunities;
        opportunities++;
    }
}

static struct Sched_Queue *pick_next_queue(void) {
    struct List_Node *node = NULL;
    if (s_current_queue_node != NULL) {
        node = &s_current_queue_node->node;
    }
    for (size_t i = 0; i < 2; i++) {
        for (; node != NULL; node = node->next) {
            if ((s_current_queue_node != NULL) &&
                (&s_current_queue_node->node == node)) {
                continue;
            }
            struct Sched_Queue *queue = node->data;
            assert(queue);
            if ((queue->opportunities != 0) && (queue->threads.front != NULL)) {
                break;
            }
        }
        /* If we couldn't find any queues, reset to beginning and try again. */
        if ((node == NULL) && (i == 0)) {
            node = s_queues.front;
        }
    }

    if (node == NULL) {
        node = s_queues.front;
        if (node == NULL) {
            return NULL;
        }
        reset_queues();
    }
    struct Sched_Queue *queue = node->data;
    assert(queue);
    queue->opportunities--;
    s_current_queue_node = queue;
    return queue;
}

static struct Thread *pick_next_task_from_mutex_waitlist(void) {
    struct Thread *result = NULL;

    LIST_FOREACH(&s_mutexwaitthreads, threadnode) {
        struct Thread *thread = threadnode->data;
        assert(thread->waitingmutex != NULL);
        if (__Mutex_TryLock(thread->waitingmutex, thread->desired_locksource)) {
            List_RemoveNode(&s_mutexwaitthreads, &thread->sched_listnode);
            result = thread;
            if (result->shutdown) {
                Co_Printf("sched: thread is about to shutdown - unlocking mutex\n");
                Mutex_Unlock(thread->waitingmutex);
            }
            thread->waitingmutex = NULL;
            /*
             * Since the list was mutated inside loop, we must get out of
             * here
             */
            break;
        }
    }
    return result;
}

static struct Thread *pick_next_task(void) {
    bool prev_interrupts = Arch_Irq_Disable();
    struct Thread *result = NULL;

    while (1) {
        result = pick_next_task_from_mutex_waitlist();
        if (result == NULL) {
            do {
                struct Sched_Queue *queue = pick_next_queue();
                if (queue == NULL) {
                    break;
                }
                struct List_Node *node = List_RemoveBack(&queue->threads);
                if (node == NULL) {
                    break;
                }
                result = node->data;
            } while (0);
        }
        if ((result == NULL) || (!result->shutdown)) {
            break;
        }
        Co_Printf("sched: shutting down thread %p\n", result);
        Thread_Delete(result);
        result = NULL;
    }
    Arch_Irq_Restore(prev_interrupts);
    return result;
}

void Sched_PrintQueues(void) {
    Co_Printf("----- QUEUE LIST -----\n");
    LIST_FOREACH(&s_queues, queuenode) {
        struct Sched_Queue *queue = queuenode->data;
        Co_Printf("queue %p - Pri %d [threads exist: %d]\n", queue, queue->priority, queue->threads.front != NULL);
        LIST_FOREACH(&queue->threads, threadnode) {
            Co_Printf(" - thread %p\n", threadnode);
        }
    }
}

void Sched_WaitMutex(struct Mutex *mutex, struct SourceLocation const *locksource) {
    assert(mutex->locked);
    bool prev_interrupts = Arch_Irq_Disable();
    memcpy(&s_runningthread->desired_locksource, locksource, sizeof(*locksource));
    struct Thread *nextthread = pick_next_task();
    if (nextthread == NULL) {
        Co_Printf("sched: WARNING: there is no thread to wait for mutex\n");
        Co_Printf("mutex is currently locked by %s:%d (%s)\n", mutex->locksource.filename, mutex->locksource.line, mutex->locksource.function);
        Co_Printf("lock requested by %s:%d (%s)\n", locksource->filename, locksource->line, locksource->function);
        Sched_PrintQueues();
        while (1) {
        }
        goto out;
    }
    assert(s_runningthread != NULL);
    assert(s_runningthread->waitingmutex == NULL);
    s_runningthread->waitingmutex = mutex;
    List_InsertFront(&s_mutexwaitthreads, &s_runningthread->sched_listnode, s_runningthread);
    struct Thread *oldthread = s_runningthread;
    assert(nextthread != oldthread);
    s_runningthread = nextthread;
    Thread_Switch(oldthread, nextthread);
out:
    Arch_Irq_Restore(prev_interrupts);
}

[[nodiscard]] int Sched_Queue(struct Thread *thread) {
    int ret = 0;
    bool prev_interrupts = Arch_Irq_Disable();
    struct Sched_Queue *queue = get_queue(thread->priority);
    if (queue == NULL) {
        ret = -ENOMEM;
        goto out;
    }
    List_InsertFront(&queue->threads, &thread->sched_listnode, thread);
    goto out;
out:
    Arch_Irq_Restore(prev_interrupts);
    return ret;
}

void Sched_Schedule(void) {
    bool prev_interrupts = Arch_Irq_Disable();
    struct Thread *nextthread = pick_next_task();
    if (nextthread == NULL) {
        goto out;
    }
    /*
     * Note that it is safe to call Sched_Schedule() before calling
     * sched_initbootthread(), as long as it's called before creating
     * any other threads.
     * If there are no other threads to switch, we will never reach here,
     * so it doesn't trip below assertion.
     */
    assert(s_runningthread != NULL);
    int ret = Sched_Queue(s_runningthread);
    if (ret < 0) {
        Co_Printf("sched: failed to queue current thread(error %d)\n", ret);
    }
    struct Thread *oldthread = s_runningthread;
    assert(oldthread != NULL);
    assert(nextthread != oldthread);
    s_runningthread = nextthread;
    Thread_Switch(oldthread, nextthread);
out:
    Arch_Irq_Restore(prev_interrupts);
}

void Sched_InitBootThread(void) {
    assert(s_runningthread == NULL);
    /*
     * Init values for thread is not used, as those are only used for fresh new
     * threads, and boot thread is what we are running now.
     */
    s_runningthread = Thread_Create(0, NULL, NULL);
    assert(s_runningthread != NULL);
    s_runningthread->priority = BOOT_THREAD_PRIORITY;
}
