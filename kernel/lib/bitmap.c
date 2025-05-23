#include <kernel/lib/bitmap.h>
#include <kernel/lib/miscmath.h>
#include <kernel/types.h>
#include <stddef.h>
#include <stdint.h>

static UINT const WORD_ALL_ONES = ~0U;

static long findfirstsetbit(UINT word, long startpos) {
    if ((startpos < 0) || ((long)BITS_PER_WORD <= startpos)) {
        return -1;
    }
    UINT shifted = word >> (UINT)startpos;
    if (shifted == 0) {
        return -1;
    }
    long index = startpos;
    for (; (shifted & 1U) == 0; shifted >>= 1U, index++) {
        if (index == (BITS_PER_WORD - 1)) {
            return -1;
        }
    }
    return index;
}

static long findlastcontiguousbit(UINT word, long startpos) {
    if ((startpos < 0) || ((long)BITS_PER_WORD <= startpos)) {
        return -1;
    }
    if (word == WORD_ALL_ONES) {
        return BITS_PER_WORD - 1;
    }
    UINT shifted = word >> (UINT)startpos;
    if ((shifted & 0x1U) == 0) {
        return -1;
    }
    long index = startpos;
    for (; (shifted & 1U) != 0; shifted >>= 1U, index++) {
    }
    return index - 1;
}

UINT make_bitmask(size_t offset, size_t len) {
    if (len == 0) {
        return 0;
    }
    return (WORD_ALL_ONES >> (BITS_PER_WORD - len)) << offset;
}

size_t bitmap_needed_word_count(size_t bits) {
    return size_to_blocks(bits, BITS_PER_WORD);
}

long bitmap_find_first_set_bit(struct bitmap *self, long startpos) {
    if (startpos < 0) {
        return -1;
    }
    size_t start_word_idx = startpos / BITS_PER_WORD;
    long bitoffset = startpos % (long)BITS_PER_WORD;
    for (size_t word_idx = start_word_idx; word_idx < self->word_count; word_idx++, bitoffset = 0) {
        long idx = findfirstsetbit(self->words[word_idx], bitoffset);
        if (0 <= idx) {
            return (long)(word_idx * BITS_PER_WORD) + idx;
        }
    }
    return -1;
}

long bitmap_find_last_contiguous_bit(struct bitmap *self, long startpos) {
    if (startpos < 0) {
        return -1;
    }
    size_t start_word_idx = startpos / BITS_PER_WORD;
    long bitoffset = startpos % (long)BITS_PER_WORD;
    if (self->word_count <= start_word_idx) {
        return -1;
    }
    for (size_t word_idx = start_word_idx; word_idx < self->word_count;
         word_idx++, bitoffset = 0) {
        long idx = findlastcontiguousbit(self->words[word_idx], bitoffset);
        if (idx < 0) {
            return -1;
        }
        if (
            /* We found 1 before the MSB */
            (idx != (BITS_PER_WORD - 1)) ||
            /* We found 1 at MSB, and it doesn't continue on the next word. */
            ((idx == (BITS_PER_WORD - 1)) && ((word_idx + 1) < self->word_count) && ((self->words[word_idx + 1] & 0x1U) == 0))) {
            return (long)(word_idx * BITS_PER_WORD) + idx;
        }
    }
    return (long)(self->word_count * BITS_PER_WORD) - 1;
}

long bitmap_find_set_bits(struct bitmap *self, long startpos, size_t minlen) {
    if (startpos < 0) {
        return -1;
    }
    long first_bit_idx = startpos;
    long last_bit_idx;
    for (;; first_bit_idx = last_bit_idx + 1) {
        first_bit_idx = bitmap_find_first_set_bit(self, first_bit_idx);
        if (first_bit_idx < 0) {
            return -1;
        }
        last_bit_idx = bitmap_find_last_contiguous_bit(self, first_bit_idx);
        size_t foundlen = last_bit_idx - first_bit_idx + 1;
        if (minlen <= foundlen) {
            return first_bit_idx;
        }
    }
}

bool bitmap_are_bits_set(struct bitmap *self, long offset, size_t len) {
    if ((offset < 0) || (len == 0)) {
        return false;
    }
    size_t word_idx = offset / BITS_PER_WORD;
    size_t bitoffset = offset % BITS_PER_WORD;
    size_t remaininglen = len;
    size_t currentlen;
    for (; remaininglen != 0; remaininglen -= currentlen, word_idx++, bitoffset = 0) {
        if (self->word_count <= word_idx) {
            return false;
        }
        UINT word = self->words[word_idx];
        if (word == 0) {
            return false;
        }
        currentlen = remaininglen;
        if (BITS_PER_WORD < (bitoffset + currentlen)) {
            currentlen = BITS_PER_WORD - bitoffset;
        }
        UINT mask = make_bitmask(bitoffset, currentlen);
        if ((word & mask) != mask) {
            return false;
        }
    }
    return true;
}

void bitmap_set_bits(struct bitmap *self, long offset, size_t len) {
    if (len == 0) {
        return;
    }
    size_t word_idx = offset / BITS_PER_WORD;
    size_t bitoffset = offset % BITS_PER_WORD;
    size_t remaininglen = len;
    size_t currentlen;
    for (; remaininglen != 0; remaininglen -= currentlen, word_idx++, bitoffset = 0) {
        if (self->word_count <= word_idx) {
            break;
        }
        currentlen = remaininglen;
        if (BITS_PER_WORD < (bitoffset + currentlen)) {
            currentlen = BITS_PER_WORD - bitoffset;
        }
        UINT mask = make_bitmask(bitoffset, currentlen);
        self->words[word_idx] |= mask;
    }
}

void bitmap_clear_bits(struct bitmap *self, long offset, size_t len) {
    if (len == 0) {
        return;
    }
    size_t word_idx = offset / BITS_PER_WORD;
    size_t bitoffset = offset % BITS_PER_WORD;
    size_t remaininglen = len;
    size_t currentlen;
    for (; remaininglen != 0; remaininglen -= currentlen, word_idx++, bitoffset = 0) {
        if (self->word_count <= word_idx) {
            break;
        }
        currentlen = remaininglen;
        if (BITS_PER_WORD < (bitoffset + currentlen)) {
            currentlen = BITS_PER_WORD - bitoffset;
        }
        UINT mask = make_bitmask(bitoffset, currentlen);
        self->words[word_idx] &= ~mask;
    }
}

void bitmap_set_bit(struct bitmap *self, long offset) {
    if ((offset < 0) || (self->word_count < (offset / BITS_PER_WORD))) {
        return;
    }
    self->words[offset / BITS_PER_WORD] |= (1U << (offset % BITS_PER_WORD));
}

void bitmap_clear_bit(struct bitmap *self, long offset) {
    if ((offset < 0) || (self->word_count < (offset / BITS_PER_WORD))) {
        return;
    }
    self->words[offset / BITS_PER_WORD] &= ~(1U << (offset % BITS_PER_WORD));
}

bool bitmap_is_bit_set(struct bitmap *self, long offset) {
    if ((offset < 0) || (self->word_count < (offset / BITS_PER_WORD))) {
        return false;
    }
    return self->words[offset / BITS_PER_WORD] & (1U << (offset % BITS_PER_WORD));
}
