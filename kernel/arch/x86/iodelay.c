#include <kernel/arch/iodelay.h>
#include <stddef.h>
#include <stdint.h>

void arch_iodelay(void) {
    __asm__ volatile("outb %al, $0x80");
}
