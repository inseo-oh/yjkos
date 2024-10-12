#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef uint32_t bitword_t;
typedef int32_t bitindex_t; // Negative -> Invalid index

#define BITS_PER_WORD            (sizeof(bitword_t) * 8)

bitword_t makebitmask(size_t offset, size_t len);

struct bitmap {
    bitword_t *words;
    size_t wordcount;
};

// When bits cannot be found, it returns -1.

size_t bitmap_neededwordcount(size_t bits);
bitindex_t bitmap_findfirstsetbit(struct bitmap *self, bitindex_t startpos);
bitindex_t bitmap_findlastcontiguousbit(struct bitmap *self, bitindex_t startpos);
bitindex_t bitmap_findsetbits(struct bitmap *self, bitindex_t startpos, size_t minlen);
bool bitmap_arebitsset(struct bitmap *bitmap, bitindex_t offset, size_t len);
void bitmap_setbits(struct bitmap *self, bitindex_t offset, size_t len);
void bitmap_clearbits(struct bitmap *self, bitindex_t offset, size_t len);

void bitmap_setbit(struct bitmap *self, bitindex_t offset);
void bitmap_clearbit(struct bitmap *self, bitindex_t offset);
bool bitmap_isbitset(struct bitmap *self, bitindex_t offset);
