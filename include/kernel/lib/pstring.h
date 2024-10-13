#pragma once
#include <kernel/types.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

// memcpy, memset for physical pointers.
void pmemcpy_in(void *dest, physptr src, size_t len, bool nocache);
void pmemcpy_out(physptr dest, void const *src, size_t len, bool nocache);
void pmemset(physptr dest, int byte, size_t len, bool nocache);

uint8_t ppeek8(physptr at, bool nocache);
uint16_t ppeek16(physptr at, bool nocache);
uint32_t ppeek32(physptr at, bool nocache);

void ppoke8(physptr to, uint8_t val, bool nocache);
void ppoke16(physptr to, uint16_t val, bool nocache);
void ppoke32(physptr to, uint32_t val, bool nocache);

void pmemcpy(physptr dest, physptr src, size_t len, bool nocache);

