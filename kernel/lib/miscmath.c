#include <assert.h>
#include <kernel/lib/miscmath.h>
#include <stddef.h>
#include <stdint.h>

bool IsAligned(size_t x, size_t align) {
    assert(align != 0);
    return (x % align) == 0;
}

size_t AlignUp(size_t x, size_t align) {
    assert(align != 0);
    if (IsAligned(x, align)) {
        return x;
    }
    return x + (align - (x % align));
}

size_t AlignDown(size_t x, size_t align) {
    assert(align != 0);
    return x - (x % align);
}

bool IsPtrAligned(void *x, size_t align) {
    assert(align != 0);
    return ((uintptr_t)x % align) == 0;
}

void *AlignPtrUp(void *x, size_t align) {
    assert(align != 0);
    if (IsPtrAligned(x, align)) {
        return x;
    }
    return (char *)x + (align - ((uintptr_t)x % align));
}

void *AlignPtrDown(void *x, size_t align) {
    assert(align != 0);
    return (char *)x - ((uintptr_t)x % align);
}

size_t SizeToBlocks(size_t size, size_t block_size) {
    assert(block_size != 0);
    if ((size % block_size) == 0) {
        return size / block_size;
    }
    return size / block_size + 1;
}

uint16_t Uint16LeAt(uint8_t const *ptr) {
    return ((uint32_t)ptr[1] << 8U) | (uint32_t)ptr[0];
}

uint32_t Uint32LeAt(uint8_t const *ptr) {
    return ((uint32_t)ptr[3] << 24U) |
           ((uint32_t)ptr[2] << 16U) |
           ((uint32_t)ptr[1] << 8U) |
           (uint32_t)ptr[0];
}
