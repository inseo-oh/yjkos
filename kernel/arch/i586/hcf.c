#include <kernel/arch/hcf.h>

[[noreturn]] void Arch_Hcf(void) {
    __asm__ volatile(
        "cli\n"
        "0:\n"
        "   hlt\n"
        "   jmp 0b\n");
    while (1) {
    }
}
