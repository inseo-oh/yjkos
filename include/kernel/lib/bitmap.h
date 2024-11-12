#pragma once
#include <kernel/types.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BITS_PER_WORD            (sizeof(uint) * 8)

uint makebitmask(size_t offset, size_t len);

struct bitmap {
    uint *words;
    size_t wordcount;
};

// When bits cannot be found, it returns -1.
size_t bitmap_neededwordcount(size_t bits);
long bitmap_findfirstsetbit(struct bitmap *self, long startpos);
long bitmap_findlastcontiguousbit(struct bitmap *self, long startpos);
long bitmap_findsetbits(struct bitmap *self, long startpos, size_t minlen);
bool bitmap_arebitsset(struct bitmap *bitmap, long offset, size_t len);
void bitmap_setbits(struct bitmap *self, long offset, size_t len);
void bitmap_clearbits(struct bitmap *self, long offset, size_t len);

void bitmap_setbit(struct bitmap *self, long offset);
void bitmap_clearbit(struct bitmap *self, long offset);
bool bitmap_isbitset(struct bitmap *self, long offset);
