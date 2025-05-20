#pragma once
#include <kernel/lib/diagnostics.h>

struct mutex {
    struct source_location locksource;
    _Atomic bool locked;
};

void mutex_init(struct mutex *out);
[[nodiscard]] bool __mutex_try_lock(struct mutex *self, struct source_location loc);
void __mutex_lock(struct mutex *self, struct source_location loc);
#define MUTEX_TRYLOCK(self) __mutex_try_lock(self, SOURCELOCATION_CURRENT())
#define MUTEX_LOCK(self) __mutex_lock(self, SOURCELOCATION_CURRENT())

void mutex_unlock(struct mutex *self);
