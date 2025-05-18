#pragma once
#include <kernel/arch/mmu.h>
#include <kernel/types.h>
#include <stddef.h>
#include <stdint.h>

/* memcpy, memset for physical pointers. */
void PMemCopyIn(void *dest, PHYSPTR src, size_t len, MMU_CACHE_INHIBIT cache_inhibit);
void PMemCopyOut(PHYSPTR dest, void const *src, size_t len, MMU_CACHE_INHIBIT cache_inhibit);
void PMemSet(PHYSPTR dest, int byte, size_t len, MMU_CACHE_INHIBIT cache_inhibit);

uint8_t PPeek8(PHYSPTR at, MMU_CACHE_INHIBIT cache_inhibit);
uint16_t PPekk16(PHYSPTR at, MMU_CACHE_INHIBIT cache_inhibit);
uint32_t PPeek32(PHYSPTR at, MMU_CACHE_INHIBIT cache_inhibit);

void PPoke8(PHYSPTR to, uint8_t val, MMU_CACHE_INHIBIT cache_inhibit);
void PPoke16(PHYSPTR to, uint16_t val, MMU_CACHE_INHIBIT cache_inhibit);
void PPoke32(PHYSPTR to, uint32_t val, MMU_CACHE_INHIBIT cache_inhibit);

void PMemCopy(PHYSPTR dest, PHYSPTR src, size_t len, MMU_CACHE_INHIBIT cache_inhibit);
