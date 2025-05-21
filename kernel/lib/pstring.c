#include <kernel/arch/interrupts.h>
#include <kernel/arch/mmu.h>
#include <kernel/lib/miscmath.h>
#include <kernel/lib/pstring.h>
#include <kernel/lib/strutil.h>
#include <kernel/types.h>
#include <stddef.h>
#include <stdint.h>

void pmemcpy_in(void *dest, PHYSPTR src, size_t len, MMU_CACHE_INHIBIT cache_inhibit) {
    bool prev_interrupts = arch_irq_disable();
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
        arch_mmu_scratch_map(srcpage, cache_inhibit);
        memcpy(dest_byte, (char *)ARCH_SCRATCH_MAP_BASE + offset, copylen);
    }
    arch_irq_restore(prev_interrupts);
}

void pmemcpy_out(PHYSPTR dest, void const *src, size_t len, MMU_CACHE_INHIBIT cache_inhibit) {
    bool prev_interrupts = arch_irq_disable();
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
        arch_mmu_scratch_map(destpage, cache_inhibit);
        memcpy((char *)ARCH_SCRATCH_MAP_BASE + offset, src_byte, copylen);
    }
    arch_irq_restore(prev_interrupts);
}

void pmemset(PHYSPTR dest, int byte, size_t len, MMU_CACHE_INHIBIT cache_inhibit) {
    bool prev_interrupts = arch_irq_disable();
    PHYSPTR destpage = align_down(dest, ARCH_PAGESIZE);
    size_t offset = dest - destpage;
    size_t copylen;
    for (size_t i = 0; i < len; destpage += ARCH_PAGESIZE, i += copylen, offset = 0) {
        size_t maxlen = ARCH_PAGESIZE - offset;
        copylen = len - i;
        if (maxlen < copylen) {
            copylen = maxlen;
        }
        arch_mmu_scratch_map(destpage, cache_inhibit);
        memset((char *)ARCH_SCRATCH_MAP_BASE + offset, byte, copylen);
    }
    arch_irq_restore(prev_interrupts);
}

uint8_t ppeek8(PHYSPTR at, MMU_CACHE_INHIBIT cache_inhibit) {
    uint8_t result;
    pmemcpy_in(&result, at, sizeof(result), cache_inhibit);
    return result;
}

uint16_t ppeek16(PHYSPTR at, MMU_CACHE_INHIBIT cache_inhibit) {
    uint16_t result;
    pmemcpy_in(&result, at, sizeof(result), cache_inhibit);
    return result;
}

uint32_t ppeek32(PHYSPTR at, MMU_CACHE_INHIBIT cache_inhibit) {
    uint32_t result;
    pmemcpy_in(&result, at, sizeof(result), cache_inhibit);
    return result;
}

void ppoke8(PHYSPTR to, uint8_t val, MMU_CACHE_INHIBIT cache_inhibit) {
    pmemcpy_out(to, &val, sizeof(val), cache_inhibit);
}

void ppoke16(PHYSPTR to, uint16_t val, MMU_CACHE_INHIBIT cache_inhibit) {
    pmemcpy_out(to, &val, sizeof(val), cache_inhibit);
}

void ppoke32(PHYSPTR to, uint32_t val, MMU_CACHE_INHIBIT cache_inhibit) {
    pmemcpy_out(to, &val, sizeof(val), cache_inhibit);
}

void pmemcpy(PHYSPTR dest, PHYSPTR src, size_t len, MMU_CACHE_INHIBIT cache_inhibit) {
    for (size_t i = 0; i < len; i++) {
        ppoke8(dest + i, ppeek8(src + i, cache_inhibit), cache_inhibit);
    }
}
