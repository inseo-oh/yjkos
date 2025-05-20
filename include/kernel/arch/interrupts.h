#pragma once

typedef enum {
    IRQSTATE_DISABLED,
    IRQSTATE_ENABLED,
} ARCH_IRQSTATE;

ARCH_IRQSTATE arch_irq_are_enabled(void);
ARCH_IRQSTATE arch_irq_enable(void);
ARCH_IRQSTATE arch_irq_disable(void);

#define ASSERT_IRQ_DISABLED() assert(!arch_irq_are_enabled())

static inline void arch_irq_restore(ARCH_IRQSTATE prev_state) {
    if (prev_state == IRQSTATE_ENABLED) {
        arch_irq_enable();
    }
}
