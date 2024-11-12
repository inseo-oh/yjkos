#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define WILL_ADD_OVERFLOW(_base, _offset, _max) \
    (((_max) - (_base)) < (_offset))

bool is_aligned(size_t x, size_t align);
size_t align_up(size_t x, size_t align);
size_t align_down(size_t x, size_t align);
bool is_ptr_aligned(void *x, size_t align);
void *align_ptr_up(void *x, size_t align);
void *align_ptr_down(void *x, size_t align);
size_t size_to_blocks(size_t size, size_t blocksize);

uint16_t uint16_le_at(uint8_t const *ptr);
uint32_t uint32_le_at(uint8_t const *ptr);
