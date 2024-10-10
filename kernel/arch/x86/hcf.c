#include <kernel/arch/hcf.h>
#include <kernel/lib/noreturn.h>

NORETURN void arch_hcf(void) {
    __asm__ volatile(
        "cli\n"
        "0:\n"
        "   hlt\n"
        "   jmp 0b\n"
    );
    while(1);
}
