#pragma once
#include <kernel/types.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void pmm_register(PHYSPTR base, size_t page_count);
/*
 * Returns NULL on failure
 */
PHYSPTR pmm_alloc(size_t *pagecount_inout);
void pmm_free(PHYSPTR ptr, size_t page_count);
size_t pmm_get_totalmem(void);

bool pmm_pagepool_test_random(void);
