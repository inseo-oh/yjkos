#include <kernel/arch/interrupts.h>
#include <kernel/arch/mmu.h>
#include <kernel/lib/miscmath.h>
#include <kernel/lib/pstring.h>
#include <kernel/types.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

void pmemcpy_in(void *dest, PHYSPTR src, size_t len, bool nocache) {
    bool prev_interrupts = arch_interrupts_disable();
    PHYSPTR srcpage = align_down(src, ARCH_PAGESIZE);
    size_t offset = src - srcpage;
    uint8_t *dest_byte = dest;
    size_t copylen;
    for (size_t i = 0; i < len; dest_byte += copylen, srcpage += ARCH_PAGESIZE, i += copylen, offset = 0) {
        copylen = len - i;
        size_t maxlen = ARCH_PAGESIZE - offset;
        if (maxlen < copylen) {
            copylen = maxlen;
        }
        arch_mmu_scratchmap(srcpage, nocache);
        memcpy(dest_byte, (char *)ARCH_SCRATCH_MAP_BASE + offset, copylen);
    }
    interrupts_restore(prev_interrupts);
}

void pmemcpy_out(PHYSPTR dest, void const *src, size_t len, bool nocache) {
    bool prev_interrupts = arch_interrupts_disable();
    PHYSPTR destpage = align_down(dest, ARCH_PAGESIZE);
    size_t offset = dest - destpage;
    uint8_t const *src_byte = src;
    size_t copylen;
    for (size_t i = 0; i < len; src_byte += copylen, destpage += ARCH_PAGESIZE, i += copylen, offset = 0) {
        size_t maxlen = ARCH_PAGESIZE - offset;
        copylen = len - i;
        if (maxlen < copylen) {
            copylen = maxlen;
        }
        arch_mmu_scratchmap(destpage, nocache);
        memcpy((char *)ARCH_SCRATCH_MAP_BASE + offset, src_byte, copylen);
    }
    interrupts_restore(prev_interrupts);
}

void pmemset(PHYSPTR dest, int byte, size_t len, bool nocache) {
    bool prev_interrupts = arch_interrupts_disable();
    PHYSPTR destpage = align_down(dest, ARCH_PAGESIZE);
    size_t offset = dest - destpage;
    size_t copylen;
    for (size_t i = 0; i < len; destpage += ARCH_PAGESIZE, i += copylen, offset = 0) {
        size_t maxlen = ARCH_PAGESIZE - offset;
        copylen = len - i;
        if (maxlen < copylen) {
            copylen = maxlen;
        }
        arch_mmu_scratchmap(destpage, nocache);
        memset((char *)ARCH_SCRATCH_MAP_BASE + offset, byte, copylen);
    }
    interrupts_restore(prev_interrupts);
}

uint8_t ppeek8(PHYSPTR at, bool nocache) {
    uint8_t result;
    pmemcpy_in(&result, at, sizeof(result), nocache);
    return result;
}

uint16_t ppeek16(PHYSPTR at, bool nocache) {
    uint16_t result;
    pmemcpy_in(&result, at, sizeof(result), nocache);
    return result;
}

uint32_t ppeek32(PHYSPTR at, bool nocache) {
    uint32_t result;
    pmemcpy_in(&result, at, sizeof(result), nocache);
    return result;
}

void ppoke8(PHYSPTR to, uint8_t val, bool nocache) {
    pmemcpy_out(to, &val, sizeof(val), nocache);
}

void ppoke16(PHYSPTR to, uint16_t val, bool nocache) {
    pmemcpy_out(to, &val, sizeof(val), nocache);
}

void ppoke32(PHYSPTR to, uint32_t val, bool nocache) {
    pmemcpy_out(to, &val, sizeof(val), nocache);
}

void pmemcpy(PHYSPTR dest, PHYSPTR src, size_t len, bool nocache) {
    for (size_t i = 0; i < len; i++) {
        ppoke8(dest + i, ppeek8(src + i, nocache), nocache);
    }
}
