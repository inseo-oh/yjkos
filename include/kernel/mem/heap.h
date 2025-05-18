#pragma once
#include <kernel/lib/diagnostics.h>
#include <stddef.h>
#include <stdint.h>

static uint8_t const HEAP_FLAG_ZEROMEMORY = 1 << 0;

void __Heap_CheckOverflow(struct SourceLocation srcloc);
#define HEAP_CHECKOVERFLOW() __Heap_CheckOverflow(SOURCELOCATION_CURRENT())
void *Heap_Alloc(size_t size, uint8_t flags);
void Heap_Free(void *ptr);
void Heap_Expand(void);
void *Heap_Realloc(void *ptr, size_t newsize, uint8_t flags);
void *Heap_Calloc(size_t size, size_t elements, uint8_t flags);
void *Heap_ReallocArray(void *ptr, size_t newsize, size_t newelements, uint8_t flags);

bool Heap_RunRandomTest(void);
