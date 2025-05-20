#pragma once
#include <kernel/lib/diagnostics.h>
#include <stddef.h>
#include <stdint.h>

static uint8_t const HEAP_FLAG_ZEROMEMORY = 1 << 0;

void __heap_check_overflow(struct source_location srcloc);
#define HEAP_CHECKOVERFLOW() __heap_check_overflow(SOURCELOCATION_CURRENT())
void *heap_alloc(size_t size, uint8_t flags);
void heap_free(void *ptr);
void heap_expand(void);
void *heap_realloc(void *ptr, size_t newsize, uint8_t flags);
void *heap_calloc(size_t size, size_t elements, uint8_t flags);
void *heap_realloc_array(void *ptr, size_t newsize, size_t newelements, uint8_t flags);

bool heap_run_random_test(void);
