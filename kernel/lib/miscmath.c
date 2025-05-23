#include <assert.h>
#include <kernel/lib/miscmath.h>
#include <stddef.h>
#include <stdint.h>

bool is_aligned(size_t x, size_t align) {
    assert(align != 0);
    return (x % align) == 0;
}

size_t align_up(size_t x, size_t align) {
    assert(align != 0);
    if (is_aligned(x, align)) {
        return x;
    }
    return x + (align - (x % align));
}

size_t align_down(size_t x, size_t align) {
    assert(align != 0);
    return x - (x % align);
}

bool is_ptr_aligned(void *x, size_t align) {
    assert(align != 0);
    return ((uintptr_t)x % align) == 0;
}

void *align_ptr_up(void *x, size_t align) {
    assert(align != 0);
    if (is_ptr_aligned(x, align)) {
        return x;
    }
    return (char *)x + (align - ((uintptr_t)x % align));
}

void *align_ptr_down(void *x, size_t align) {
    assert(align != 0);
    return (char *)x - ((uintptr_t)x % align);
}

size_t size_to_blocks(size_t size, size_t block_size) {
    assert(block_size != 0);
    if ((size % block_size) == 0) {
        return size / block_size;
    }
    return size / block_size + 1;
}

uint16_t u16le_at(uint8_t const *ptr) {
    return ((uint32_t)ptr[1] << 8U) | (uint32_t)ptr[0];
}

uint32_t u32le_at(uint8_t const *ptr) {
    return ((uint32_t)ptr[3] << 24U) |
           ((uint32_t)ptr[2] << 16U) |
           ((uint32_t)ptr[1] << 8U) |
           (uint32_t)ptr[0];
}
