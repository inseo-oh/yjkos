#pragma once
#include <kernel/types.h>
#include <stddef.h>
#include <stdint.h>

#define BITS_PER_WORD (sizeof(UINT) * 8)

UINT MakeBitmask(size_t offset, size_t len);

struct Bitmap {
    UINT *words;
    size_t word_count;
};

/* When bits cannot be found, the function returns -1. */

size_t Bitmap_NeededWordCount(size_t bits);
long Bitmap_FindFirstSetBit(struct Bitmap *self, long startpos);
long Bitmap_FindLastContiguousBit(struct Bitmap *self, long startpos);
long Bitmap_FindSetBits(struct Bitmap *self, long startpos, size_t minlen);
bool Bitmap_AreBitsSet(struct Bitmap *bitmap, long offset, size_t len);
void Bitmap_SetBits(struct Bitmap *self, long offset, size_t len);
void Bitmap_ClearBits(struct Bitmap *self, long offset, size_t len);

void Bitmap_SetBit(struct Bitmap *self, long offset);
void Bitmap_ClearBit(struct Bitmap *self, long offset);
bool Bitmap_IsBitSet(struct Bitmap *self, long offset);
