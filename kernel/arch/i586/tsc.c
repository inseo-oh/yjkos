#include "asm/i586.h"
#include <kernel/arch/tsc.h>
#include <stdint.h>

uint64_t arch_read_tsc(void) {
    uint32_t upper;
    uint32_t lower;
    archi586_rdtsc(&upper, &lower);
    return ((uint64_t)upper << 32) | (uint64_t)lower;
}
