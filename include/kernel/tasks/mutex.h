#pragma once
#include <kernel/lib/diagnostics.h>
#include <stdbool.h>

struct mutex {
    struct sourcelocation locksource;
    _Atomic bool locked;
};

void mutex_init(struct mutex *out);
WARN_UNUSED_RESULT bool __mutex_trylock(
    struct mutex *self, struct sourcelocation loc);
void __mutex_lock(
    struct mutex *self, struct sourcelocation loc);
#define MUTEX_TRYLOCK(self) __mutex_trylock(self, SOURCELOCATION_CURRENT())
#define MUTEX_LOCK(self)    __mutex_lock(self, SOURCELOCATION_CURRENT())

void mutex_unlock(struct mutex *self);
