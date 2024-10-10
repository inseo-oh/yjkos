#pragma once
#include <kernel/lib/diagnostics.h>
#include <kernel/status.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct queue queue_t;
struct queue {
    void *buf;
    size_t enqueueindex, dequeueindex, itemsize, cap;
    bool lastwasenqueue : 1;
};

void queue_init(queue_t *Queue, void *buf, size_t itemsize, size_t cap);
bool queue_isfull(queue_t const *self);
bool queue_isempty(queue_t const *self);

FAILABLE_FUNCTION queue_enqueue_impl(queue_t *self, void const *data, size_t itemsize);
WARN_UNUSED_RESULT bool queue_dequeue_impl(void *out_buf, queue_t *self, size_t itemsize);
WARN_UNUSED_RESULT void *queue_peek(queue_t const *self, size_t itemsize);

#define QUEUE_ENQUEUE(_self, _data)     queue_enqueue_impl((_self), (_data), sizeof(*(_data)))
#define QUEUE_DEQUEUE(_out_buf, _self)  queue_dequeue_impl((_out_buf), (_self), sizeof(*(_out_buf)))

// Helper macro for initialize array-backed queues.
#define QUEUE_INIT_FOR_ARRAY(_queue, _buf)    queue_init((_queue), (_buf), sizeof(*_buf), sizeof(_buf)/sizeof(*_buf))
