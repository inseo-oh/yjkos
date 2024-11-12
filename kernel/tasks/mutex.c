#include <kernel/arch/interrupts.h>
#include <kernel/lib/diagnostics.h>
#include <kernel/tasks/mutex.h>
#include <kernel/tasks/sched.h>
#include <stdatomic.h>
#include <string.h>

void mutex_init(struct mutex *out) {
    memset(out, 0, sizeof(*out));
}

WARN_UNUSED_RESULT bool __mutex_trylock(
    struct mutex *self,
    struct sourcelocation loc)
{
    bool expected = false;
    if (!atomic_compare_exchange_strong_explicit(
        &self->locked, &expected, true,
        memory_order_acquire, memory_order_relaxed))
    {
        return false;
    }
    memcpy(&self->locksource, &loc, sizeof(self->locksource));
    return true;
}

void __mutex_lock(
    struct mutex *self, struct sourcelocation loc)
{
    assert(self);
    bool previnterrupts = arch_interrupts_disable();
    if (!__mutex_trylock(self, loc)) {
        sched_waitmutex(self, &loc);
        assert(self->locked);
    }
    interrupts_restore(previnterrupts);
}

void mutex_unlock(struct mutex *self) {
    self->locksource.filename = NULL;
    self->locksource.function = NULL;
    self->locksource.line = 0;
    atomic_store_explicit(&self->locked, false, memory_order_release);
}
