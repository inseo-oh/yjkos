#pragma once

typedef enum {
    IRQSTATE_DISABLED,
    IRQSTATE_ENABLED,
} ARCH_IRQSTATE;

ARCH_IRQSTATE Arch_Irq_AreEnabled(void);
ARCH_IRQSTATE Arch_Irq_Enable(void);
ARCH_IRQSTATE Arch_Irq_Disable(void);

#define ASSERT_IRQ_DISABLED() assert(!Arch_Irq_AreEnabled())

static inline void Arch_Irq_Restore(ARCH_IRQSTATE prev_state) {
    if (prev_state == IRQSTATE_ENABLED) {
        Arch_Irq_Enable();
    }
}
