#include "asm/i586.h"
#include <kernel/arch/interrupts.h>

ARCH_IRQSTATE Arch_Irq_AreEnabled(void) {
    return (ArchI586_GetEFlags() & EFLAGS_FLAG_IF) ? IRQSTATE_ENABLED : IRQSTATE_DISABLED;
}

ARCH_IRQSTATE Arch_Irq_Enable(void) {
    ARCH_IRQSTATE prev_state = Arch_Irq_AreEnabled();
    ArchI586_Sti();
    return prev_state;
}

ARCH_IRQSTATE Arch_Irq_Disable(void) {
    ARCH_IRQSTATE prev_state = Arch_Irq_AreEnabled();
    ArchI586_Cli();
    return prev_state;
}
