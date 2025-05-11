#pragma once
#include <kernel/types.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// memcpy, memset for physical pointers.
void pmemcpy_in(void *dest, PHYSPTR src, size_t len, bool nocache);
void pmemcpy_out(PHYSPTR dest, void const *src, size_t len, bool nocache);
void pmemset(PHYSPTR dest, int byte, size_t len, bool nocache);

uint8_t ppeek8(PHYSPTR at, bool nocache);
uint16_t ppeek16(PHYSPTR at, bool nocache);
uint32_t ppeek32(PHYSPTR at, bool nocache);

void ppoke8(PHYSPTR to, uint8_t val, bool nocache);
void ppoke16(PHYSPTR to, uint16_t val, bool nocache);
void ppoke32(PHYSPTR to, uint32_t val, bool nocache);

void pmemcpy(PHYSPTR dest, PHYSPTR src, size_t len, bool nocache);

