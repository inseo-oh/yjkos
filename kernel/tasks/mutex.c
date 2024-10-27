#include <kernel/arch/interrupts.h>
#include <kernel/io/co.h>
#include <kernel/lib/diagnostics.h>
#include <kernel/tasks/mutex.h>
#include <kernel/tasks/sched.h>
#include <string.h>
#include <stdatomic.h>

void mutex_init(struct mutex *out) {
    memset(out, 0, sizeof(*out));
}

WARN_UNUSED_RESULT bool __mutex_trylock(
    struct mutex *self,
    char const *filename, char const *func, int line)
{
    bool expected = false;
    if (!atomic_compare_exchange_strong_explicit(
        &self->locked, &expected, true,
        memory_order_acquire, memory_order_relaxed))
    {
        return false;
    }
    self->locksource.filename = filename;
    self->locksource.func = func;
    self->locksource.line = line;
    return true;
}

void __mutex_lock(
    struct mutex *self,
    char const *filename, char const *func, int line)
{
    assert(self);
    bool previnterrupts = arch_interrupts_disable();
    if (!__mutex_trylock(self, filename, func, line)) {
        struct mutex_locksource source = {
            .filename = filename,
            .func = func,
            .line = line,
        };
        sched_waitmutex(self, &source);
        assert(self);
        if (!self->locked) {
            while(1);
        }
    }
    interrupts_restore(previnterrupts);
}

void mutex_unlock(struct mutex *self) {
    self->locksource.filename = NULL;
    self->locksource.func = NULL;
    self->locksource.line = 0;
    atomic_store_explicit(&self->locked, false, memory_order_release);
}
