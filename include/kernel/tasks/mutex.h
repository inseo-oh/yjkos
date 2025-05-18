#pragma once
#include <kernel/lib/diagnostics.h>

struct Mutex {
    struct SourceLocation locksource;
    _Atomic bool locked;
};

void Mutex_Init(struct Mutex *out);
[[nodiscard]] bool __Mutex_TryLock(struct Mutex *self, struct SourceLocation loc);
void __Mutex_Lock(struct Mutex *self, struct SourceLocation loc);
#define MUTEX_TRYLOCK(self) __Mutex_TryLock(self, SOURCELOCATION_CURRENT())
#define MUTEX_LOCK(self) __Mutex_Lock(self, SOURCELOCATION_CURRENT())

void Mutex_Unlock(struct Mutex *self);
