#include <kernel/lib/diagnostics.h>
#include <kernel/tasks/mutex.h>
#include <string.h>
#include <stdatomic.h>

void mutex_init(struct mutex *out) {
    memset(out, 0, sizeof(*out));
}

WARN_UNUSED_RESULT bool mutex_trylock(struct mutex *self) {
    bool expected = false;
    return atomic_compare_exchange_strong_explicit(
        &self->flag, &expected, true,
        memory_order_acquire, memory_order_relaxed);
}

void mutex_lock(struct mutex *self) {
    // TODO: Use scheduler to wait for mutex unlock.
    while (!mutex_trylock(self)) {}
}

void mutex_unlock(struct mutex *self) {
    atomic_store_explicit(&self->flag, false, memory_order_release);
}
