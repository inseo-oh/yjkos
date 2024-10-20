#pragma once
#include <kernel/lib/diagnostics.h>
#include <stdbool.h>

struct mutex {
    _Atomic bool locked;
};

void mutex_init(struct mutex *out);
WARN_UNUSED_RESULT bool mutex_trylock(struct mutex *self);
void mutex_lock(struct mutex *self);
void mutex_unlock(struct mutex *self);
