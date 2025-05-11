#include <kernel/arch/randseed.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#define INITIAL_SEED 0xdead
static uint16_t s_randseed = INITIAL_SEED;
static uint16_t s_randlfsr = INITIAL_SEED; // Must match with above seed.

int rand(void) {
    uint32_t out = 0;
    for (int i = 0; i < 32; i++) {
        uint32_t t1 = s_randlfsr >> 0U;
        uint32_t t2 = s_randlfsr >> 2U;
        uint32_t t3 = s_randlfsr >> 3U;
        uint32_t t4 = s_randlfsr >> 5U;
        uint32_t xor = ((t1 ^ t2) ^ (t3 ^ t4)) & 0x1U;
        uint16_t shiftedbitout = s_randlfsr & 0x1;
        s_randlfsr = ((uint32_t)s_randlfsr >> 1) | (xor<< 15U);
        out = (out << 1) | shiftedbitout;
        if (s_randlfsr == s_randseed) {
            // Reset seed
            s_randseed = arch_randseed();
            s_randlfsr = s_randseed;
        }
    }
    return (int)out;
}
