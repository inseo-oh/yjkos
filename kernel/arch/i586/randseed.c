#include "asm/i586.h"
#include <kernel/arch/randseed.h>
#include <stdint.h>

uint16_t arch_randseed(void) {
    uint32_t upper, lower;
    archi586_rdtsc(&upper, &lower);
    uint16_t seed = ((lower >> 16) ^ lower);
    if (seed == 0) {
        seed++;
    }
    return seed;
}
