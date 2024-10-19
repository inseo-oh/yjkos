#pragma once
#include <stdint.h>

void archi586_contextswitch(uintptr_t *oldstackptr, uintptr_t newstackptr);
