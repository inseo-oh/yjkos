#include <kernel/arch/interrupts.h>
#include <kernel/lib/diagnostics.h>
#include <kernel/lib/strutil.h>
#include <kernel/tasks/mutex.h>
#include <kernel/tasks/sched.h>
#include <stdatomic.h>

void mutex_init(struct mutex *out) {
    vmemset(out, 0, sizeof(*out));
}

[[nodiscard]] bool __mutex_try_lock(struct mutex *self, struct source_location loc) {
    bool expected = false;
    if (!atomic_compare_exchange_strong_explicit(&self->locked, &expected, true, memory_order_acquire, memory_order_relaxed)) {
        return false;
    }
    vmemcpy(&self->locksource, &loc, sizeof(self->locksource));
    return true;
}

void __mutex_lock(struct mutex *self, struct source_location loc) {
    assert(self);
    bool prev_interrupts = arch_irq_disable();
    if (!__mutex_try_lock(self, loc)) {
        sched_wait_mutex(self, &loc);
        assert(self->locked);
    }
    arch_irq_restore(prev_interrupts);
}

void mutex_unlock(struct mutex *self) {
    self->locksource.filename = nullptr;
    self->locksource.function = nullptr;
    self->locksource.line = 0;
    atomic_store_explicit(&self->locked, false, memory_order_release);
}
