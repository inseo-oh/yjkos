#include <kernel/arch/hcf.h>

[[noreturn]] void arch_hcf(void) {
    __asm__ volatile(
        "cli\n"
        "0:\n"
        "   hlt\n"
        "   jmp 0b\n");
    while (1) {
    }
}
