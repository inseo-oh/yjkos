#pragma once
#include <stdbool.h>

bool arch_interrupts_are_enabled(void);
bool arch_interrupts_enable(void);
bool arch_interrupts_disable(void);

#define ASSERT_INTERRUPTS_DISABLED() assert(!arch_interrupts_are_enabled())

static inline void interrupts_restore(bool prevState) {
    if (prevState) {
        arch_interrupts_enable();
    }
}
