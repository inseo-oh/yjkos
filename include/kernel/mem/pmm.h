#pragma once
#include <kernel/types.h>
#include <kernel/status.h>
#include <stdalign.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

void pmm_register(physptr_t base, size_t pagecount);
FAILABLE_FUNCTION pmm_alloc(physptr_t *ptr_out, size_t *pagecount_inout);
void pmm_free(physptr_t ptr, size_t pagecount);
size_t pmm_get_totalmem(void);

bool pmm_pagepool_test_random(void);
