#include <kernel/arch/iodelay.h>
#include <stddef.h>
#include <stdint.h>

void Arch_IoDelay(void) {
    __asm__ volatile("outb %al, $0x80");
}
