#pragma once
#include <kernel/types.h>
#include <stdalign.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

void pmm_register(physptr base, size_t pagecount);
/*
 * Returns NULL on failure
 */
physptr pmm_alloc(size_t *pagecount_inout);
void pmm_free(physptr ptr, size_t pagecount);
size_t pmm_get_totalmem(void);

bool pmm_pagepool_test_random(void);
