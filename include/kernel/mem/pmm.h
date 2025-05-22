#pragma once
#include <kernel/types.h>
#include <stdalign.h>
#include <stddef.h>
#include <stdint.h>

void pmm_register_mem(PHYSPTR base, size_t page_count);
/*
 * Returns nullptr on failure
 */
PHYSPTR pmm_alloc(size_t *pagecount_inout);
void pmm_free(PHYSPTR ptr, size_t page_count);
size_t pmm_get_total_mem_size(void);

bool pmm_page_pool_test_random(void);
