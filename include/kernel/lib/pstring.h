#pragma once
#include <kernel/status.h>
#include <kernel/types.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

// memcpy, memset for physical pointers.
void pmemcpy_in(void *dest, physptr_t src, size_t len, bool nocache);
void pmemcpy_out(physptr_t dest, void const *src, size_t len, bool nocache);
void pmemset(physptr_t dest, int byte, size_t len, bool nocache);

uint8_t ppeek8(physptr_t at, bool nocache);
uint16_t ppeek16(physptr_t at, bool nocache);
uint32_t ppeek32(physptr_t at, bool nocache);

void ppoke8(physptr_t to, uint8_t val, bool nocache);
void ppoke16(physptr_t to, uint16_t val, bool nocache);
void ppoke32(physptr_t to, uint32_t val, bool nocache);

void pmemcpy(physptr_t dest, physptr_t src, size_t len, bool nocache);

