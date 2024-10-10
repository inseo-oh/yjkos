#pragma once
#include <stdint.h>

void archx86_contextswitch(uintptr_t *oldstackptr, uintptr_t newstackptr);
