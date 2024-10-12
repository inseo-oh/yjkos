#include "ioport.h"
#include <stddef.h>
#include <stdint.h>

void archx86_out8(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %%al, %%dx" ::"d"(port), "a"(val));
}

void archx86_out16(uint16_t port, uint16_t val) {
    __asm__ volatile("out %%ax, %%dx" ::"d"(port), "a"(val));
}

void archx86_out32(uint16_t port, uint32_t val) {
    __asm__ volatile("out %%eax, %%dx" ::"d"(port), "a"(val));
}

uint8_t archx86_in8(uint16_t port) {
    uint8_t result = 0;
    __asm__ volatile("in %%dx, %%al" : "=a"(result) : "d"(port));
    return result;
}

uint16_t archx86_in16(uint16_t port) {
    uint16_t result = 0;
    __asm__ volatile("in %%dx, %%ax" : "=a"(result) : "d"(port));
    return result;
}

uint32_t archx86_in32(uint16_t port) {
    uint32_t result = 0;
    __asm__ volatile("in %%dx, %%eax" : "=a"(result) : "d"(port));
    return result;
}

void archx86_in16rep(uint16_t port, void *buf, size_t len) {
    __asm__ volatile("rep insw"
             :: "d"(port), "D"(buf), "c"(len));
}
