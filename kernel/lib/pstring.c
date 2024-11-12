#include <kernel/arch/interrupts.h>
#include <kernel/arch/mmu.h>
#include <kernel/lib/miscmath.h>
#include <kernel/lib/pstring.h>
#include <kernel/types.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

void pmemcpy_in(void *dest, physptr src, size_t len, bool nocache) {
    bool previnterrupts = arch_interrupts_disable();
    physptr srcpage = align_down(src, ARCH_PAGESIZE);
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
        memcpy(
            dest_byte, (char *)ARCH_SCRATCH_MAP_BASE + offset,
            copylen);
    }
    interrupts_restore(previnterrupts);
}

void pmemcpy_out(physptr dest, void const *src, size_t len, bool nocache) {
    bool previnterrupts = arch_interrupts_disable();
    physptr destpage = align_down(dest, ARCH_PAGESIZE);
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
        memcpy(
            (char *)ARCH_SCRATCH_MAP_BASE + offset,
            src_byte, copylen);
    }
    interrupts_restore(previnterrupts);
}

void pmemset(physptr dest, int byte, size_t len, bool nocache) {
    bool previnterrupts = arch_interrupts_disable();
    physptr destpage = align_down(dest, ARCH_PAGESIZE);
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
    interrupts_restore(previnterrupts);
}

uint8_t ppeek8(physptr at, bool nocache) {
    uint8_t result;
    pmemcpy_in(&result, at, sizeof(result), nocache);
    return result;
}

uint16_t ppeek16(physptr at, bool nocache) {
    uint16_t result;
    pmemcpy_in(&result, at, sizeof(result), nocache);
    return result;
}

uint32_t ppeek32(physptr at, bool nocache) {
    uint32_t result;
    pmemcpy_in(&result, at, sizeof(result), nocache);
    return result;
}

void ppoke8(physptr to, uint8_t val, bool nocache) {
    pmemcpy_out(to, &val, sizeof(val), nocache);
}

void ppoke16(physptr to, uint16_t val, bool nocache) {
    pmemcpy_out(to, &val, sizeof(val), nocache);
}

void ppoke32(physptr to, uint32_t val, bool nocache) {
    pmemcpy_out(to, &val, sizeof(val), nocache);
}

void pmemcpy(physptr dest, physptr src, size_t len, bool nocache) {
    for (size_t i = 0; i < len; i++) {
        ppoke8(dest + i, ppeek8(src + i, nocache), nocache);
    }
}
