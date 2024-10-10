#include <kernel/lib/bitmap.h>
#include <kernel/lib/miscmath.h>
#include <stdbool.h>
#include <stdint.h>

static bitword_t const WORD_ALL_ONES = ~0;

static bitindex_t findfirstsetbit(bitword_t word, bitindex_t startpos) {
    if ((startpos < 0) || ((bitindex_t)BITS_PER_WORD <= startpos)) {
        return -1;
    }
    bitword_t shifted = word >> startpos;
    if (shifted == 0) {
        return -1;
    }
    bitindex_t index = startpos;
    for (; (shifted & 1) == 0; shifted >>= 1, index++) {
        if (index == (BITS_PER_WORD - 1)) {
            return -1;
        }
    }
    return index;
}

static bitindex_t findlastcontiguousbit(bitword_t word, bitindex_t startpos) {
    if ((startpos < 0) || ((bitindex_t)BITS_PER_WORD <= startpos)) {
        return -1;
    }
    if (word == WORD_ALL_ONES) {
        return BITS_PER_WORD - 1;
    }
    bitword_t shifted = word >> startpos;
    if ((shifted & 0x1) == 0) {
        return -1;
    }
    bitindex_t index = startpos;
    for (; (shifted & 1) != 0; shifted >>= 1, index++) {}
    return index - 1;
}

bitword_t makebitmask(size_t offset, size_t len) {
    if (len == 0) {
        return 0;
    }
    return (WORD_ALL_ONES >> (BITS_PER_WORD - len)) << offset;
}

size_t bitmap_neededwordcount(size_t bits) {
    return sizetoblocks(bits, BITS_PER_WORD);
}

bitindex_t bitmap_findfirstsetbit(bitmap_t *self, bitindex_t startpos) {
    if (startpos < 0) {
        return -1;
    }
    size_t startwordidx = startpos / BITS_PER_WORD;
    size_t bitoffset = startpos % BITS_PER_WORD;
    for (size_t wordidx = startwordidx; wordidx < self->wordcount; wordidx++, bitoffset = 0) {
        bitindex_t idx = findfirstsetbit(self->words[wordidx], bitoffset);
        if (0 <= idx) {
            return (wordidx * BITS_PER_WORD) + idx;
        }
    }
    return -1;
}

bitindex_t bitmap_findlastcontiguousbit(bitmap_t *self, bitindex_t startpos) {
    if (startpos < 0) {
        return -1;
    }
    size_t startwordidx = startpos / BITS_PER_WORD;
    size_t bitoffset = startpos % BITS_PER_WORD;
    if (self->wordcount <= startwordidx) {
        return -1;
    }
    for (size_t wordidx = startwordidx; wordidx < self->wordcount; wordidx++, bitoffset = 0) {
        bitindex_t idx = findlastcontiguousbit(self->words[wordidx], bitoffset);
        if (idx < 0) {
            return -1;
        } else if (
            // We found 1 before the MSB
            (idx != (BITS_PER_WORD - 1)) ||
            // We found 1 at MSB, and it doesn't continue on the next word.
            ((idx == (BITS_PER_WORD - 1)) && ((wordidx + 1) < self->wordcount) && ((self->words[wordidx + 1] & 0x1) == 0))
        ) {
            return (wordidx * BITS_PER_WORD) + idx;
        }
    }
    return (self->wordcount * BITS_PER_WORD) - 1;
}

bitindex_t bitmap_findsetbits(bitmap_t *self, bitindex_t startpos, size_t minlen) {
    if (startpos < 0) {
        return -1;
    }
    bitindex_t firstbitidx = startpos;
    bitindex_t lastbitidx;
    for (;; firstbitidx = lastbitidx + 1) {
        firstbitidx = bitmap_findfirstsetbit(self, firstbitidx);
        if (firstbitidx < 0) {
            return -1;
        }
        lastbitidx = bitmap_findlastcontiguousbit(self, firstbitidx);
        size_t foundlen = lastbitidx - firstbitidx + 1;
        if (minlen <= foundlen) {
            return firstbitidx;
        }
    }

}

bool bitmap_arebitsset(bitmap_t *self, bitindex_t offset, size_t len) {
    if ((offset < 0) || (len == 0)) {
        return false;
    }
    size_t wordidx = offset / BITS_PER_WORD;
    size_t bitoffset = offset % BITS_PER_WORD;
    size_t remaininglen = len;
    size_t currentlen;
    for (; remaininglen != 0; remaininglen -= currentlen, wordidx++, bitoffset = 0) {
        if (self->wordcount <= wordidx) {
            return false;
        }
        bitword_t word = self->words[wordidx];
        if (word == 0) {
            return false;
        }
        currentlen = remaininglen;
        if (BITS_PER_WORD < (bitoffset + currentlen)) {
            currentlen = BITS_PER_WORD - bitoffset;
        }
        bitword_t mask = makebitmask(bitoffset, currentlen);
        if ((word & mask) != mask) {
            return false;
        }
    }
    return true;
}


void bitmap_setbits(bitmap_t *self, bitindex_t offset, size_t len) {
    if (len == 0) {
        return;
    }
    size_t wordidx = offset / BITS_PER_WORD;
    size_t bitoffset = offset % BITS_PER_WORD;
    size_t remaininglen = len;
    size_t currentlen;
    for (; remaininglen != 0; remaininglen -= currentlen, wordidx++, bitoffset = 0) {
        if (self->wordcount <= wordidx) {
            break;
        }
        currentlen = remaininglen;
        if (BITS_PER_WORD < (bitoffset + currentlen)) {
            currentlen = BITS_PER_WORD - bitoffset;
        }
        bitword_t mask = makebitmask(bitoffset, currentlen);
        self->words[wordidx] |= mask;
    }
}

void bitmap_clearbits(bitmap_t *self, bitindex_t offset, size_t len) {
    if (len == 0) {
        return;
    }
    size_t wordidx = offset / BITS_PER_WORD;
    size_t bitoffset = offset % BITS_PER_WORD;
    size_t remaininglen = len;
    size_t currentlen;
    for (; remaininglen != 0; remaininglen -= currentlen, wordidx++, bitoffset = 0) {
        if (self->wordcount <= wordidx) {
            break;
        }
        currentlen = remaininglen;
        if (BITS_PER_WORD < (bitoffset + currentlen)) {
            currentlen = BITS_PER_WORD - bitoffset;
        }
        bitword_t mask = makebitmask(bitoffset, currentlen);
        self->words[wordidx] &= ~mask;
    }
}

void bitmap_setbit(bitmap_t *self, bitindex_t offset) {
    if ((offset < 0) || (self->wordcount < (offset / BITS_PER_WORD))) {
        return;
    }
    self->words[offset / BITS_PER_WORD] |= ((bitword_t)1 << (offset % BITS_PER_WORD));
}

void bitmap_clearbit(bitmap_t *self, bitindex_t offset) {
    if ((offset < 0) || (self->wordcount < (offset / BITS_PER_WORD))) {
        return;
    }
    self->words[offset / BITS_PER_WORD] &= ~((bitword_t)1 << (offset % BITS_PER_WORD));
}

bool bitmap_isbitset(bitmap_t *self, bitindex_t offset) {
    if ((offset < 0) || (self->wordcount < (offset / BITS_PER_WORD))) {
        return false;
    }
    return self->words[offset / BITS_PER_WORD] & ((bitword_t)1 << (offset % BITS_PER_WORD));
}
