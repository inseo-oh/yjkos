#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef uint32_t bitword_t;
typedef int32_t bitindex_t; // Negative -> Invalid index

#define BITS_PER_WORD            (sizeof(bitword_t) * 8)

bitword_t makebitmask(size_t offset, size_t len);

typedef struct bitmap bitmap_t;
struct bitmap {
    bitword_t *words;
    size_t wordcount;
};

// When bits cannot be found, it returns -1.

size_t bitmap_neededwordcount(size_t bits);
bitindex_t bitmap_findfirstsetbit(bitmap_t *self, bitindex_t startpos);
bitindex_t bitmap_findlastcontiguousbit(bitmap_t *self, bitindex_t startpos);
bitindex_t bitmap_findsetbits(bitmap_t *self, bitindex_t startpos, size_t minlen);
bool bitmap_arebitsset(bitmap_t *bitmap, bitindex_t offset, size_t len);
void bitmap_setbits(bitmap_t *self, bitindex_t offset, size_t len);
void bitmap_clearbits(bitmap_t *self, bitindex_t offset, size_t len);

void bitmap_setbit(bitmap_t *self, bitindex_t offset);
void bitmap_clearbit(bitmap_t *self, bitindex_t offset);
bool bitmap_isbitset(bitmap_t *self, bitindex_t offset);
