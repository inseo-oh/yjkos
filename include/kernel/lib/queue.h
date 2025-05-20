#pragma once
#include <kernel/lib/diagnostics.h>
#include <stddef.h>

struct queue {
    void *buf;
    size_t enqueue_index, dequeue_index, item_size, cap;
    bool last_was_enqueue : 1;
};

void queue_init(struct queue *Queue, void *buf, size_t itemsize, size_t cap);
bool queue_is_full(struct queue const *self);
bool queue_is_empty(struct queue const *self);

[[nodiscard]] int __queue_enqueue(struct queue *self, void const *data, size_t itemsize);
[[nodiscard]] bool __queue_dequeue(void *out_buf, struct queue *self, size_t itemsize);
[[nodiscard]] void *queue_peek(struct queue const *self, size_t itemsize);

#define QUEUE_ENQUEUE(_self, _data) \
    __queue_enqueue((_self), (_data), sizeof(*(_data)))
#define QUEUE_DEQUEUE(_out_buf, _self) \
    __queue_dequeue((_out_buf), (_self), sizeof(*(_out_buf)))

/* Helper macro for initialize array-backed queues. */
#define QUEUE_INIT_FOR_ARRAY(_queue, _buf) queue_init((_queue), (_buf), sizeof(*(_buf)), sizeof(_buf) / sizeof(*(_buf)))
