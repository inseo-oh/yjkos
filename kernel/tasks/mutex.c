#include <kernel/arch/interrupts.h>
#include <kernel/lib/diagnostics.h>
#include <kernel/tasks/mutex.h>
#include <kernel/tasks/sched.h>
#include <stdatomic.h>
#include <string.h>

void Mutex_Init(struct Mutex *out) {
    memset(out, 0, sizeof(*out));
}

[[nodiscard]] bool __Mutex_TryLock(struct Mutex *self, struct SourceLocation loc) {
    bool expected = false;
    if (!atomic_compare_exchange_strong_explicit(&self->locked, &expected, true, memory_order_acquire, memory_order_relaxed)) {
        return false;
    }
    memcpy(&self->locksource, &loc, sizeof(self->locksource));
    return true;
}

void __Mutex_Lock(struct Mutex *self, struct SourceLocation loc) {
    assert(self);
    bool prev_interrupts = Arch_Irq_Disable();
    if (!__Mutex_TryLock(self, loc)) {
        Sched_WaitMutex(self, &loc);
        assert(self->locked);
    }
    Arch_Irq_Restore(prev_interrupts);
}

void Mutex_Unlock(struct Mutex *self) {
    self->locksource.filename = NULL;
    self->locksource.function = NULL;
    self->locksource.line = 0;
    atomic_store_explicit(&self->locked, false, memory_order_release);
}
