#include "asm/x86.h"
#include <kernel/arch/interrupts.h>

bool arch_interrupts_areenabled(void) {
    return (archx86_geteflags() & EFLAGS_FLAG_IF);
}

bool arch_interrupts_enable(void) {
    bool prevstate = arch_interrupts_areenabled();
    archx86_sti();
    return prevstate;
}

bool arch_interrupts_disable(void) {
    bool prevstate = arch_interrupts_areenabled();
    archx86_cli();
    return prevstate;
}
