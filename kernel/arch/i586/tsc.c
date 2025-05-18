#include "asm/i586.h"
#include <kernel/arch/tsc.h>
#include <stdint.h>

uint64_t Arch_ReadTsc(void) {
    uint32_t upper;
    uint32_t lower;
    ArchI586_Rdtsc(&upper, &lower);
    return ((uint64_t)upper << 32) | (uint64_t)lower;
}
