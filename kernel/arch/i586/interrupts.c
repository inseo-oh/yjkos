#include "asm/i586.h"
#include <kernel/arch/interrupts.h>

bool arch_interrupts_are_enabled(void) {
    return (archi586_geteflags() & EFLAGS_FLAG_IF);
}

bool arch_interrupts_enable(void) {
    bool prevstate = arch_interrupts_are_enabled();
    archi586_sti();
    return prevstate;
}

bool arch_interrupts_disable(void) {
    bool prevstate = arch_interrupts_are_enabled();
    archi586_cli();
    return prevstate;
}
