#pragma once
#include <kernel/lib/diagnostics.h>
#include <stddef.h>

struct Queue {
    void *buf;
    size_t enqueue_index, dequeue_index, item_size, cap;
    bool last_was_enqueue : 1;
};

void Queue_Init(struct Queue *Queue, void *buf, size_t itemsize, size_t cap);
bool Queue_IsFull(struct Queue const *self);
bool Queue_IsEmpty(struct Queue const *self);

[[nodiscard]] int __Queue_Enqueue(struct Queue *self, void const *data, size_t itemsize);
[[nodiscard]] bool __Queue_Dequeue(void *out_buf, struct Queue *self, size_t itemsize);
[[nodiscard]] void *Queue_Peek(struct Queue const *self, size_t itemsize);

#define QUEUE_ENQUEUE(_self, _data) \
    __Queue_Enqueue((_self), (_data), sizeof(*(_data)))
#define QUEUE_DEQUEUE(_out_buf, _self) \
    __Queue_Dequeue((_out_buf), (_self), sizeof(*(_out_buf)))

/* Helper macro for initialize array-backed queues. */
#define QUEUE_INIT_FOR_ARRAY(_queue, _buf) Queue_Init((_queue), (_buf), sizeof(*(_buf)), sizeof(_buf) / sizeof(*(_buf)))
