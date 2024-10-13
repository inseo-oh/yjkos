#pragma once
#include <kernel/lib/diagnostics.h>
#include <stdbool.h>
#include <stddef.h>

struct queue {
    void *buf;
    size_t enqueueindex, dequeueindex, itemsize, cap;
    bool lastwasenqueue : 1;
};

void queue_init(struct queue *Queue, void *buf, size_t itemsize, size_t cap);
bool queue_isfull(struct queue const *self);
bool queue_isempty(struct queue const *self);

WARN_UNUSED_RESULT int queue_enqueue_impl(
    struct queue *self, void const *data, size_t itemsize);
WARN_UNUSED_RESULT bool queue_dequeue_impl(
    void *out_buf, struct queue *self, size_t itemsize);
WARN_UNUSED_RESULT void *queue_peek(struct queue const *self, size_t itemsize);

#define QUEUE_ENQUEUE(_self, _data)     \
    queue_enqueue_impl((_self), (_data), sizeof(*(_data)))
#define QUEUE_DEQUEUE(_out_buf, _self)  \
    queue_dequeue_impl((_out_buf), (_self), sizeof(*(_out_buf)))

// Helper macro for initialize array-backed queues.
#define QUEUE_INIT_FOR_ARRAY(_queue, _buf)    queue_init((_queue), (_buf), sizeof(*_buf), sizeof(_buf)/sizeof(*_buf))
