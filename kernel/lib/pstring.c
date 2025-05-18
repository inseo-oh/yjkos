#include <kernel/arch/interrupts.h>
#include <kernel/arch/mmu.h>
#include <kernel/lib/miscmath.h>
#include <kernel/lib/pstring.h>
#include <kernel/types.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

void PMemCopyIn(void *dest, PHYSPTR src, size_t len, MMU_CACHE_INHIBIT cache_inhibit) {
    bool prev_interrupts = Arch_Irq_Disable();
    PHYSPTR srcpage = AlignDown(src, ARCH_PAGESIZE);
    size_t offset = src - srcpage;
    uint8_t *dest_byte = dest;
    size_t copylen;
    for (size_t i = 0; i < len; dest_byte += copylen, srcpage += ARCH_PAGESIZE, i += copylen, offset = 0) {
        copylen = len - i;
        size_t maxlen = ARCH_PAGESIZE - offset;
        if (maxlen < copylen) {
            copylen = maxlen;
        }
        Arch_Mmu_ScratchMap(srcpage, cache_inhibit);
        memcpy(dest_byte, (char *)ARCH_SCRATCH_MAP_BASE + offset, copylen);
    }
    Arch_Irq_Restore(prev_interrupts);
}

void PMemCopyOut(PHYSPTR dest, void const *src, size_t len, MMU_CACHE_INHIBIT cache_inhibit) {
    bool prev_interrupts = Arch_Irq_Disable();
    PHYSPTR destpage = AlignDown(dest, ARCH_PAGESIZE);
    size_t offset = dest - destpage;
    uint8_t const *src_byte = src;
    size_t copylen;
    for (size_t i = 0; i < len; src_byte += copylen, destpage += ARCH_PAGESIZE, i += copylen, offset = 0) {
        size_t maxlen = ARCH_PAGESIZE - offset;
        copylen = len - i;
        if (maxlen < copylen) {
            copylen = maxlen;
        }
        Arch_Mmu_ScratchMap(destpage, cache_inhibit);
        memcpy((char *)ARCH_SCRATCH_MAP_BASE + offset, src_byte, copylen);
    }
    Arch_Irq_Restore(prev_interrupts);
}

void PMemSet(PHYSPTR dest, int byte, size_t len, MMU_CACHE_INHIBIT cache_inhibit) {
    bool prev_interrupts = Arch_Irq_Disable();
    PHYSPTR destpage = AlignDown(dest, ARCH_PAGESIZE);
    size_t offset = dest - destpage;
    size_t copylen;
    for (size_t i = 0; i < len; destpage += ARCH_PAGESIZE, i += copylen, offset = 0) {
        size_t maxlen = ARCH_PAGESIZE - offset;
        copylen = len - i;
        if (maxlen < copylen) {
            copylen = maxlen;
        }
        Arch_Mmu_ScratchMap(destpage, cache_inhibit);
        memset((char *)ARCH_SCRATCH_MAP_BASE + offset, byte, copylen);
    }
    Arch_Irq_Restore(prev_interrupts);
}

uint8_t PPeek8(PHYSPTR at, MMU_CACHE_INHIBIT cache_inhibit) {
    uint8_t result;
    PMemCopyIn(&result, at, sizeof(result), cache_inhibit);
    return result;
}

uint16_t PPekk16(PHYSPTR at, MMU_CACHE_INHIBIT cache_inhibit) {
    uint16_t result;
    PMemCopyIn(&result, at, sizeof(result), cache_inhibit);
    return result;
}

uint32_t PPeek32(PHYSPTR at, MMU_CACHE_INHIBIT cache_inhibit) {
    uint32_t result;
    PMemCopyIn(&result, at, sizeof(result), cache_inhibit);
    return result;
}

void PPoke8(PHYSPTR to, uint8_t val, MMU_CACHE_INHIBIT cache_inhibit) {
    PMemCopyOut(to, &val, sizeof(val), cache_inhibit);
}

void PPoke16(PHYSPTR to, uint16_t val, MMU_CACHE_INHIBIT cache_inhibit) {
    PMemCopyOut(to, &val, sizeof(val), cache_inhibit);
}

void PPoke32(PHYSPTR to, uint32_t val, MMU_CACHE_INHIBIT cache_inhibit) {
    PMemCopyOut(to, &val, sizeof(val), cache_inhibit);
}

void PMemCopy(PHYSPTR dest, PHYSPTR src, size_t len, MMU_CACHE_INHIBIT cache_inhibit) {
    for (size_t i = 0; i < len; i++) {
        PPoke8(dest + i, PPeek8(src + i, cache_inhibit), cache_inhibit);
    }
}
