#include <errno.h>
#include <kernel/lib/diagnostics.h>
#include <kernel/lib/queue.h>
#include <stdint.h>
#include <string.h>

void Queue_Init(struct Queue *queue, void *buf, size_t itemsize, size_t cap) {
    memset(queue, 0, sizeof(*queue));
    queue->item_size = itemsize;
    queue->cap = cap;
    queue->buf = buf;
}

bool Queue_IsFull(struct Queue const *self) {
    return ((self->enqueue_index == self->dequeue_index) && (self->last_was_enqueue));
}

bool Queue_IsEmpty(struct Queue const *self) {
    return ((self->enqueue_index == self->dequeue_index) && (!self->last_was_enqueue));
}

[[nodiscard]] int __Queue_Enqueue(struct Queue *self, void const *data, size_t itemsize) {
    assert(itemsize == self->item_size);
    if (Queue_IsFull(self)) {
        return -ENOMEM;
    }
    size_t enqueueindex = (self->enqueue_index + 1) % self->cap;
    memcpy((((char *)self->buf) + (self->enqueue_index * itemsize)), data, itemsize);
    self->enqueue_index = enqueueindex;
    self->last_was_enqueue = true;
    return 0;
}

[[nodiscard]] bool __Queue_Dequeue(void *out_buf, struct Queue *self, size_t itemsize) {
    assert(itemsize == self->item_size);
    if (Queue_IsEmpty(self)) {
        return false;
    }
    size_t dequeueindex = (self->dequeue_index + 1) % self->cap;
    memcpy(out_buf, ((char *)self->buf) + (self->dequeue_index * itemsize), itemsize);
    self->dequeue_index = dequeueindex;
    self->last_was_enqueue = false;
    return true;
}

[[nodiscard]] void *Queue_Peek(struct Queue const *self, size_t itemsize) {
    assert(itemsize == self->item_size);
    if (Queue_IsEmpty(self)) {
        return NULL;
    }
    return ((char *)self->buf) + (self->dequeue_index * itemsize);
}
