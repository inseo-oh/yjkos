#pragma once
#include <stddef.h>
#include <stdint.h>

#define WILL_ADD_OVERFLOW(_base, _offset, _max) (((_max) - (_base)) < (_offset))

bool IsAligned(size_t x, size_t align);
size_t AlignUp(size_t x, size_t align);
size_t AlignDown(size_t x, size_t align);
bool IsPtrAligned(void *x, size_t align);
void *AlignPtrUp(void *x, size_t align);
void *AlignPtrDown(void *x, size_t align);
size_t SizeToBlocks(size_t size, size_t block_size);

uint16_t Uint16LeAt(uint8_t const *ptr);
uint32_t Uint32LeAt(uint8_t const *ptr);
