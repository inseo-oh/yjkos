#include <assert.h>
#include <kernel/lib/miscmath.h>
#include <stdint.h>

bool isaligned(size_t x, size_t align) {
    assert(align != 0);
    return (x % align) == 0;
}

size_t alignup(size_t x, size_t align) {
    assert(align != 0);
    if (isaligned(x, align)) {
        return x;
    }
    return x + (align - (x % align));
}

size_t aligndown(size_t x, size_t align) {
    assert(align != 0);
    return x - (x % align);
}

size_t sizetoblocks(size_t size, size_t blocksize) {
    assert(blocksize != 0);
    if ((size % blocksize) == 0) {
        return size / blocksize;
    } else {
        return size / blocksize + 1;
    }
}

uint16_t uint16leat(uint8_t const *ptr) {
    return ((uint32_t)ptr[1] << 8) | (uint32_t)ptr[0];
}

uint32_t uint32leat(uint8_t const *ptr) {
    return ((uint32_t)ptr[3] << 24) | ((uint32_t)ptr[2] << 16) | ((uint32_t)ptr[1] << 8) | (uint32_t)ptr[0];
}
