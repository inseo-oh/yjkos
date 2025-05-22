#include <assert.h>
#include <errno.h>
#include <kernel/arch/interrupts.h>
#include <kernel/io/co.h>
#include <kernel/lib/diagnostics.h>
#include <kernel/lib/list.h>
#include <kernel/lib/strutil.h>
#include <kernel/mem/heap.h>
#include <kernel/tasks/mutex.h>
#include <kernel/tasks/sched.h>
#include <kernel/tasks/thread.h>
#include <stdint.h>
#include <stdlib.h>

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
static struct list s_queues;
static struct sched_queue *s_current_queue_node;
static struct thread *s_runningthread;
static struct list s_mutexwaitthreads;

/*
 * If insert_after is nullptr, queue will be inserted at front.
 */
[[nodiscard]] static struct sched_queue *alloc_queue(struct sched_queue *insert_after, int8_t priority) {
    struct sched_queue *resultqueue = heap_alloc(sizeof(*resultqueue), HEAP_FLAG_ZEROMEMORY);
    if (resultqueue == nullptr) {
        return nullptr;
    }
    resultqueue->priority = priority;
    if (insert_after == nullptr) {
        list_insert_front(&s_queues, &resultqueue->node, resultqueue);
    } else {
        list_insert_after(&s_queues, &insert_after->node, &resultqueue->node, resultqueue);
    }
    return resultqueue;
}

[[nodiscard]] static struct sched_queue *get_queue(int8_t priority) {
    struct list_node *insert_after = nullptr;
    struct sched_queue *result_queue = nullptr;
    bool should_insert_front = false;
    struct sched_queue *queue = list_get_data_or_null(s_queues.front);
    if (queue != nullptr) {
        if (priority < queue->priority) {
            should_insert_front = true;
        }
    }
    if (!should_insert_front) {
        LIST_FOREACH(&s_queues, queue_node) {
            struct sched_queue *queue = queue_node->data;
            assert(queue);
            struct sched_queue *next_queue = list_get_data_or_null(queue_node->next);
            /* Found the queue */
            if (queue->priority == priority) {
                result_queue = queue;
                break;
            }
            /* No more queue */
            if (next_queue == nullptr) {
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
    if (result_queue != nullptr) {
        return result_queue;
    }
    return alloc_queue(list_get_data_or_null(insert_after), priority);
}

static void reset_queues(void) {
    /*
     * We have to reset the scheduler either because we are scheduling for
     * the first time, or every queue ran out of opportunities.
     */

    /* Reset opportunities. */
    size_t opportunities = 1;
    LIST_FOREACH(&s_queues, queuenode) {
        struct sched_queue *queue = queuenode->data;
        assert(queue);
        queue->opportunities = opportunities;
        opportunities++;
    }
}

static struct sched_queue *pick_next_queue(void) {
    struct list_node *node = nullptr;
    if (s_current_queue_node != nullptr) {
        node = &s_current_queue_node->node;
    }
    for (size_t i = 0; i < 2; i++) {
        for (; node != nullptr; node = node->next) {
            if ((s_current_queue_node != nullptr) &&
                (&s_current_queue_node->node == node)) {
                continue;
            }
            struct sched_queue *queue = node->data;
            assert(queue);
            if ((queue->opportunities != 0) && (queue->threads.front != nullptr)) {
                break;
            }
        }
        /* If we couldn't find any queues, reset to beginning and try again. */
        if ((node == nullptr) && (i == 0)) {
            node = s_queues.front;
        }
    }

    if (node == nullptr) {
        node = s_queues.front;
        if (node == nullptr) {
            return nullptr;
        }
        reset_queues();
    }
    struct sched_queue *queue = node->data;
    assert(queue);
    queue->opportunities--;
    s_current_queue_node = queue;
    return queue;
}

static struct thread *pick_next_task_from_mutex_waitlist(void) {
    struct thread *result = nullptr;

    LIST_FOREACH(&s_mutexwaitthreads, threadnode) {
        struct thread *thread = threadnode->data;
        assert(thread->waitingmutex != nullptr);
        if (__mutex_try_lock(thread->waitingmutex, thread->desired_locksource)) {
            list_remove_node(&s_mutexwaitthreads, &thread->sched_listnode);
            result = thread;
            if (result->shutdown) {
                co_printf("sched: thread is about to shutdown - unlocking mutex\n");
                mutex_unlock(thread->waitingmutex);
            }
            thread->waitingmutex = nullptr;
            /*
             * Since the list was mutated inside loop, we must get out of
             * here
             */
            break;
        }
    }
    return result;
}

static struct thread *pick_next_task(void) {
    bool prev_interrupts = arch_irq_disable();
    struct thread *result = nullptr;

    while (1) {
        result = pick_next_task_from_mutex_waitlist();
        if (result == nullptr) {
            do {
                struct sched_queue *queue = pick_next_queue();
                if (queue == nullptr) {
                    break;
                }
                struct list_node *node = list_remove_back(&queue->threads);
                if (node == nullptr) {
                    break;
                }
                result = node->data;
            } while (0);
        }
        if ((result == nullptr) || (!result->shutdown)) {
            break;
        }
        co_printf("sched: shutting down thread %p\n", result);
        thread_delete(result);
        result = nullptr;
    }
    arch_irq_restore(prev_interrupts);
    return result;
}

void sched_print_queues(void) {
    co_printf("----- QUEUE LIST -----\n");
    LIST_FOREACH(&s_queues, queuenode) {
        struct sched_queue *queue = queuenode->data;
        co_printf("queue %p - Pri %d [threads exist: %d]\n", queue, queue->priority, queue->threads.front != nullptr);
        LIST_FOREACH(&queue->threads, threadnode) {
            co_printf(" - thread %p\n", threadnode);
        }
    }
}

void sched_wait_mutex(struct mutex *mutex, struct source_location const *locksource) {
    assert(mutex->locked);
    bool prev_interrupts = arch_irq_disable();
    vmemcpy(&s_runningthread->desired_locksource, locksource, sizeof(*locksource));
    struct thread *nextthread = pick_next_task();
    if (nextthread == nullptr) {
        co_printf("sched: WARNING: there is no thread to wait for mutex\n");
        co_printf("mutex is currently locked by %s:%d (%s)\n", mutex->locksource.filename, mutex->locksource.line, mutex->locksource.function);
        co_printf("lock requested by %s:%d (%s)\n", locksource->filename, locksource->line, locksource->function);
        sched_print_queues();
        while (1) {
        }
        goto out;
    }
    assert(s_runningthread != nullptr);
    assert(s_runningthread->waitingmutex == nullptr);
    s_runningthread->waitingmutex = mutex;
    list_insert_front(&s_mutexwaitthreads, &s_runningthread->sched_listnode, s_runningthread);
    struct thread *oldthread = s_runningthread;
    assert(nextthread != oldthread);
    s_runningthread = nextthread;
    thread_switch(oldthread, nextthread);
out:
    arch_irq_restore(prev_interrupts);
}

[[nodiscard]] int sched_queue(struct thread *thread) {
    int ret = 0;
    bool prev_interrupts = arch_irq_disable();
    struct sched_queue *queue = get_queue(thread->priority);
    if (queue == nullptr) {
        ret = -ENOMEM;
        goto out;
    }
    list_insert_front(&queue->threads, &thread->sched_listnode, thread);
    goto out;
out:
    arch_irq_restore(prev_interrupts);
    return ret;
}

void sched_schedule(void) {
    bool prev_interrupts = arch_irq_disable();
    struct thread *nextthread = pick_next_task();
    if (nextthread == nullptr) {
        goto out;
    }
    /*
     * Note that it is safe to call sched_schedule() before calling
     * sched_initbootthread(), as long as it's called before creating
     * any other threads.
     * If there are no other threads to switch, we will never reach here,
     * so it doesn't trip below assertion.
     */
    assert(s_runningthread != nullptr);
    int ret = sched_queue(s_runningthread);
    if (ret < 0) {
        co_printf("sched: failed to queue current thread(error %d)\n", ret);
    }
    struct thread *oldthread = s_runningthread;
    assert(oldthread != nullptr);
    assert(nextthread != oldthread);
    s_runningthread = nextthread;
    thread_switch(oldthread, nextthread);
out:
    arch_irq_restore(prev_interrupts);
}

void sched_init_boot_thread(void) {
    assert(s_runningthread == nullptr);
    /*
     * Init values for thread is not used, as those are only used for fresh new
     * threads, and boot thread is what we are running now.
     */
    s_runningthread = thread_create(0, nullptr, nullptr);
    assert(s_runningthread != nullptr);
    s_runningthread->priority = BOOT_THREAD_PRIORITY;
}
