#include "ioport.h"
#include <stddef.h>
#include <stdint.h>

void ArchI586_Out8(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %%al, %%dx" ::"d"(port), "a"(val));
}

void ArchI586_Out16(uint16_t port, uint16_t val) {
    __asm__ volatile("out %%ax, %%dx" ::"d"(port), "a"(val));
}

void ArchI586_Out32(uint16_t port, uint32_t val) {
    __asm__ volatile("out %%eax, %%dx" ::"d"(port), "a"(val));
}

uint8_t ArchI586_In8(uint16_t port) {
    uint8_t result = 0;
    __asm__ volatile("in %%dx, %%al" : "=a"(result) : "d"(port));
    return result;
}

uint16_t ArchI586_In16(uint16_t port) {
    uint16_t result = 0;
    __asm__ volatile("in %%dx, %%ax" : "=a"(result) : "d"(port));
    return result;
}

uint32_t ArchI586_In32(uint16_t port) {
    uint32_t result = 0;
    __asm__ volatile("in %%dx, %%eax" : "=a"(result) : "d"(port));
    return result;
}

void ArchI586_In16Rep(uint16_t port, void *buf, size_t len) {
    __asm__ volatile("rep insw" :: "d"(port), "D"(buf), "c"(len));
}
