#include "asm/i586.h"
#include <kernel/arch/randseed.h>
#include <stdint.h>

uint16_t Arch_RandSeed(void) {
    uint32_t upper;
    uint32_t lower;
    ArchI586_Rdtsc(&upper, &lower);
    uint16_t seed = ((lower >> 16) ^ lower);
    if (seed == 0) {
        seed++;
    }
    return seed;
}
