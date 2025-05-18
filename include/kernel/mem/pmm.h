#pragma once
#include <kernel/types.h>
#include <stdalign.h>
#include <stddef.h>
#include <stdint.h>

void Pmm_RegisterMem(PHYSPTR base, size_t page_count);
/*
 * Returns NULL on failure
 */
PHYSPTR Pmm_Alloc(size_t *pagecount_inout);
void Pmm_Free(PHYSPTR ptr, size_t page_count);
size_t Pmm_GetTotalMem(void);

bool Pmm_PagePoolTestRandom(void);
