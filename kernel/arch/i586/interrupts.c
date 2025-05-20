#include "asm/i586.h"
#include <kernel/arch/interrupts.h>

ARCH_IRQSTATE arch_irq_are_enabled(void) {
    return (archi586_get_eflags() & EFLAGS_FLAG_IF) ? IRQSTATE_ENABLED : IRQSTATE_DISABLED;
}

ARCH_IRQSTATE arch_irq_enable(void) {
    ARCH_IRQSTATE prev_state = arch_irq_are_enabled();
    archi586_sti();
    return prev_state;
}

ARCH_IRQSTATE arch_irq_disable(void) {
    ARCH_IRQSTATE prev_state = arch_irq_are_enabled();
    archi586_cli();
    return prev_state;
}
