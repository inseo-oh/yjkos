#include <errno.h>
#include <kernel/lib/diagnostics.h>
#include <kernel/lib/queue.h>
#include <kernel/lib/strutil.h>
#include <stdint.h>

void queue_init(struct queue *queue, void *buf, size_t itemsize, size_t cap) {
    vmemset(queue, 0, sizeof(*queue));
    queue->item_size = itemsize;
    queue->cap = cap;
    queue->buf = buf;
}

bool queue_is_full(struct queue const *self) {
    return ((self->enqueue_index == self->dequeue_index) && (self->last_was_enqueue));
}

bool queue_is_empty(struct queue const *self) {
    return ((self->enqueue_index == self->dequeue_index) && (!self->last_was_enqueue));
}

[[nodiscard]] int __queue_enqueue(struct queue *self, void const *data, size_t itemsize) {
    assert(itemsize == self->item_size);
    if (queue_is_full(self)) {
        return -ENOMEM;
    }
    size_t enqueueindex = (self->enqueue_index + 1) % self->cap;
    vmemcpy((((char *)self->buf) + (self->enqueue_index * itemsize)), data, itemsize);
    self->enqueue_index = enqueueindex;
    self->last_was_enqueue = true;
    return 0;
}

[[nodiscard]] bool __queue_dequeue(void *out_buf, struct queue *self, size_t itemsize) {
    assert(itemsize == self->item_size);
    if (queue_is_empty(self)) {
        return false;
    }
    size_t dequeueindex = (self->dequeue_index + 1) % self->cap;
    vmemcpy(out_buf, ((char *)self->buf) + (self->dequeue_index * itemsize), itemsize);
    self->dequeue_index = dequeueindex;
    self->last_was_enqueue = false;
    return true;
}

[[nodiscard]] void *queue_peek(struct queue const *self, size_t itemsize) {
    assert(itemsize == self->item_size);
    if (queue_is_empty(self)) {
        return nullptr;
    }
    return ((char *)self->buf) + (self->dequeue_index * itemsize);
}
