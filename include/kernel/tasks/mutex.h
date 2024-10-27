#pragma once
#include <kernel/lib/diagnostics.h>
#include <stdbool.h>

struct mutex_locksource {
    char const *filename;
    char const *func;
    int line;
};

struct mutex {
    struct mutex_locksource locksource;
    _Atomic bool locked;
};

void mutex_init(struct mutex *out);
WARN_UNUSED_RESULT bool __mutex_trylock(
    struct mutex *self,
    char const *filename, char const *func, int line);
void __mutex_lock(
    struct mutex *self,
    char const *filename, char const *func, int line);
#define MUTEX_TRYLOCK(self) __mutex_trylock(self, __FILE__, __func__, __LINE__)
#define MUTEX_LOCK(self)    __mutex_lock(self, __FILE__, __func__, __LINE__)

void mutex_unlock(struct mutex *self);
