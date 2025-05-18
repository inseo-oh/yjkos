#include <kernel/arch/randseed.h>
#include <stdint.h>
#include <stdlib.h>

#define INITIAL_SEED 0xdead
static uint16_t s_randseed = INITIAL_SEED;
static uint16_t s_rand_lfsr = INITIAL_SEED; /* Must match with above seed. */

int rand(void) {
    uint32_t out = 0;
    for (int i = 0; i < 32; i++) {
        uint32_t t1 = s_rand_lfsr >> 0U;
        uint32_t t2 = s_rand_lfsr >> 2U;
        uint32_t t3 = s_rand_lfsr >> 3U;
        uint32_t t4 = s_rand_lfsr >> 5U;
        uint32_t xor = ((t1 ^ t2) ^ (t3 ^ t4)) & 0x1U;
        uint16_t shiftedbitout = s_rand_lfsr & 0x1;
        s_rand_lfsr = ((uint32_t)s_rand_lfsr >> 1) | (xor<< 15U);
        out = (out << 1) | shiftedbitout;
        if (s_rand_lfsr == s_randseed) {
            /* Reset seed */
            s_randseed = Arch_RandSeed();
            s_rand_lfsr = s_randseed;
        }
    }
    return (int)out;
}
