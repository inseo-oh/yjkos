#include <kernel/lib/diagnostics.h>
#include <kernel/lib/queue.h>
#include <kernel/status.h>
#include <stdint.h>
#include <string.h>

void queue_init(struct queue *queue, void *buf, size_t itemsize, size_t cap) {
    memset(queue, 0, sizeof(*queue));
    queue->itemsize = itemsize;
    queue->cap = cap;
    queue->buf = buf;
}

bool queue_isfull(struct queue const *self) {
    return ((self->enqueueindex == self->dequeueindex) && (self->lastwasenqueue));
}

bool queue_isempty(struct queue const *self) {
    return ((self->enqueueindex == self->dequeueindex) && (!self->lastwasenqueue));
}

FAILABLE_FUNCTION queue_enqueue_impl(struct queue *self, void const *data, size_t itemsize) {
FAILABLE_PROLOGUE
    assert(itemsize == self->itemsize);
    if (queue_isfull(self)) {
        THROW(ERR_NOMEM);
    }
    size_t enqueueindex = (self->enqueueindex + 1) % self->cap;
    memcpy((void *)(((uintptr_t)self->buf) + (self->enqueueindex * itemsize)), data, itemsize);
    self->enqueueindex = enqueueindex;
    self->lastwasenqueue = true;
FAILABLE_EPILOGUE_BEGIN
FAILABLE_EPILOGUE_END
}

WARN_UNUSED_RESULT bool queue_dequeue_impl(void *out_buf, struct queue *self, size_t itemsize) {
    assert(itemsize == self->itemsize);
    if (queue_isempty(self)) {
        return false;
    }
    size_t dequeueindex = (self->dequeueindex + 1) % self->cap;
    memcpy(out_buf, (void *)(((uintptr_t)self->buf) + (self->dequeueindex * itemsize)), itemsize);
    self->dequeueindex = dequeueindex;
    self->lastwasenqueue = false;
    return true;
}

WARN_UNUSED_RESULT void *queue_peek(struct queue const *self, size_t itemsize) {
    assert(itemsize == self->itemsize);
    if (queue_isempty(self)) {
        return NULL;
    }
    return (void *)(((uintptr_t)self->buf) + (self->dequeueindex * itemsize));
}
