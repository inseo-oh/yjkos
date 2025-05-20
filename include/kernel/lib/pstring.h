#pragma once
#include <kernel/arch/mmu.h>
#include <kernel/types.h>
#include <stddef.h>
#include <stdint.h>

/* memcpy, memset for physical pointers. */
void pmemcpy_in(void *dest, PHYSPTR src, size_t len, MMU_CACHE_INHIBIT cache_inhibit);
void pmemcpy_out(PHYSPTR dest, void const *src, size_t len, MMU_CACHE_INHIBIT cache_inhibit);
void pmemset(PHYSPTR dest, int byte, size_t len, MMU_CACHE_INHIBIT cache_inhibit);

uint8_t ppeek8(PHYSPTR at, MMU_CACHE_INHIBIT cache_inhibit);
uint16_t ppeek16(PHYSPTR at, MMU_CACHE_INHIBIT cache_inhibit);
uint32_t ppeek32(PHYSPTR at, MMU_CACHE_INHIBIT cache_inhibit);

void ppoke8(PHYSPTR to, uint8_t val, MMU_CACHE_INHIBIT cache_inhibit);
void ppoke16(PHYSPTR to, uint16_t val, MMU_CACHE_INHIBIT cache_inhibit);
void ppoke32(PHYSPTR to, uint32_t val, MMU_CACHE_INHIBIT cache_inhibit);

void pmemcpy(PHYSPTR dest, PHYSPTR src, size_t len, MMU_CACHE_INHIBIT cache_inhibit);
