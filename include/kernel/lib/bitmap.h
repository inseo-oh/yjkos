#pragma once
#include <kernel/types.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BITS_PER_WORD (sizeof(UINT) * 8)

UINT make_bitmask(size_t offset, size_t len);

struct bitmap {
    UINT *words;
    size_t wordcount;
};

// When bits cannot be found, it returns -1.
size_t bitmap_needed_word_count(size_t bits);
long bitmap_find_first_set_bit(struct bitmap *self, long startpos);
long bitmap_find_last_contiguous_bit(struct bitmap *self, long startpos);
long bitmap_find_set_bits(struct bitmap *self, long startpos, size_t minlen);
bool bitmap_are_bits_set(struct bitmap *bitmap, long offset, size_t len);
void bitmap_set_bits(struct bitmap *self, long offset, size_t len);
void bitmap_clear_bits(struct bitmap *self, long offset, size_t len);

void bitmap_set_bit(struct bitmap *self, long offset);
void bitmap_clear_bit(struct bitmap *self, long offset);
bool bitmap_is_bit_set(struct bitmap *self, long offset);
