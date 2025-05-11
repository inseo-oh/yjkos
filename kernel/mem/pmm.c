#include <assert.h>
#include <kernel/arch/interrupts.h>
#include <kernel/arch/mmu.h>
#include <kernel/io/co.h>
#include <kernel/lib/bitmap.h>
#include <kernel/lib/diagnostics.h>
#include <kernel/lib/pstring.h>
#include <kernel/mem/heap.h>
#include <kernel/mem/pmm.h>
#include <kernel/panic.h>
#include <kernel/types.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

//------------------------------- Configuration -------------------------------

// Print when a pool initializes?
static bool const CONFIG_PRINT_POOL_INIT = true;
/*
 * Should we run sequential allocation test when a pool initializes?
 * This can take a *very* long time depending on CPU speed and how large the
 * pool is.
 */
static bool const CONFIG_TEST_POOL = false;

//-----------------------------------------------------------------------------

struct pagepool {
    struct pagepool *nextpool;
    struct bitmap bitmap;
    PHYSPTR baseaddr;
    size_t page_count;
    uint8_t levelcount;
    UINT bitmapdata[];
};

static struct pagepool *s_firstpool;

// Physical memory management is done using buddy allocation algorithm.
// There are several levels in metadata area, and each level corresponds to
// specific allocation size, which is also size of the block.
//
// If there are N pages:
// -    Level 0: Each block consists of N   pages, and the level has 1 page.
// -    Level 1: Each block consists of N/2 pages, and the level has 2 pages.
// -    Level 2: Each block consists of N/4 pages, and the level has 4 pages.
// - ...
// - Last level: Each block consists of 1   page,  and the level has N pages.
//
// Note that each level's single block at index M is the same as next level's
// two blocks at index (M * 2). In other words:
// Level 0 | 00000000 |
// Level 1 | 00001111 |
// Level 2 | 00112233 |
// Level 3 | 01234567 |
// ...
//
// Each block is either available(1) or not(0), and this info is stored in bitmap in the metadata area.
// Initially blocks at all levels are unavailable, except for the very first level.
// When allocating blocks, it first calculates right level for the given size, and then
// looks for the suitable block in that level.
// If it wasn't found, it keeps decreasing level until something is found. Then it splits blocks:
// 1. Mark the block as unavailable
// 2. Move to the next level, and mark corresponding blocks as available.
// 3. If we haven't reached the level for given size, go to 1.
//    (In this case we will use of the blocks we just marked as available above)
// 4. Return one of the blocks we made above(by marking it as unavilable).
//
// Deallocating works in reverse(who's surprised?).
// 1. Mark the block as available.
// 2. If neighbor block is also available, mark both blocks as unavailable, decrease level, and mark
//    the corresponding block as available.
// 3. Repeat 2 until neighbor is not marked as available, or there is no neighbor(i.e. The first level).

static void calculate_pagepoolsizes(size_t *pagecount_out, size_t *levelcount_out, size_t *bitcount_out, size_t page_count) {
    size_t currentpagecount = page_count;
    size_t resultpagecount = 1;
    size_t levelcount = 1;
    size_t bitcount = 1;
    while (1 < currentpagecount) {
        bitcount += currentpagecount;
        currentpagecount /= 2;
        resultpagecount *= 2;
        levelcount++;
    }
    *pagecount_out = resultpagecount;
    *levelcount_out = levelcount;
    *bitcount_out = bitcount;
}

static void bit_indices_range_for_level(long *start_out, long *end_out, size_t level) {
    size_t currentlevel = 0;
    long blockinlevel = 1;
    long bitoffset = 0;
    while (currentlevel < level) {
        bitoffset += blockinlevel;
        blockinlevel *= 2;
        currentlevel++;
    }
    *start_out = bitoffset;
    *end_out = bitoffset + blockinlevel - 1;
}

static long bit_index_for_pagepool_block(size_t level, size_t block) {
    long start = 0;
    long end = 0;
    bit_indices_range_for_level(&start, &end, level);
    size_t blocksinlevel = end - start + 1;
    assert(block < blocksinlevel);
    return start + (long)block;
}

static size_t blocksize_to_pagepool_level(struct pagepool const *pool, size_t size) {
    size_t sizeperblock = pool->page_count;
    size_t currentlevel = 0;
    while (size < sizeperblock) {
        currentlevel++;
        sizeperblock /= 2;
    }
    return currentlevel;
}

/*
 * Returns NULL on allocation failure
 */
static PHYSPTR alloc_from_pool(struct pagepool *pool, size_t *pagecount_inout) {
    assert(*pagecount_inout != 0);
    PHYSPTR result = PHYSICALPTR_NULL;
    // Adjust page size to nearest 2^n
    size_t block_size = 1;
    while (block_size < *pagecount_inout) {
        if ((SIZE_MAX / 2) < block_size) {
            goto fail_oom;
        }
        block_size *= 2;
    }
    if (pool->page_count < block_size) {
        goto fail_oom;
    }
    *pagecount_inout = block_size;
    size_t wanted_level = blocksize_to_pagepool_level(pool, block_size);
    size_t found_level = wanted_level;
    size_t found_blockindex = 0;
    while (1) {
        long bit_start = 0;
        long bit_end = 0;
        bit_indices_range_for_level(&bit_start, &bit_end, found_level);
        long found_at = bitmap_find_set_bits(&pool->bitmap, bit_start, 1);
        if ((0 <= found_at) && (found_at <= bit_end)) {
            // Found block
            found_blockindex = found_at - bit_start;
            break;
        }
        if (found_level == 0) {
            goto fail_oom;
        }
        found_level--;
    }
    // If we found block at lower level, split blocks until we reach there.
    size_t current_block_index = found_blockindex;
    for (size_t currentlevel = found_level; currentlevel < wanted_level; currentlevel++, current_block_index *= 2) {
        // Mark current one as unavilable.
        long bit_index = bit_index_for_pagepool_block(currentlevel, current_block_index);
        bitmap_clear_bit(&pool->bitmap, bit_index);
        // Mark upper block's second block as available.
        // (First block will be marked as unavailable next time, so don't touch that)
        bit_index = bit_index_for_pagepool_block(currentlevel + 1, (current_block_index * 2) + 1);
        bitmap_set_bit(&pool->bitmap, bit_index);
    }
    // Mark resulting block as unavailable
    long bit_index = bit_index_for_pagepool_block(wanted_level, current_block_index);
    bitmap_clear_bit(&pool->bitmap, bit_index);
    result = pool->baseaddr + (block_size * current_block_index * ARCH_PAGESIZE);
    goto out;
fail_oom:
    result = PHYSICALPTR_NULL;
out:
    return result;
}

static void free_from_pool(struct pagepool *pool, PHYSPTR ptr, size_t page_count) {
    if (ptr == 0) {
        return;
    }
    if ((ptr < pool->baseaddr) || (pool->baseaddr + (ARCH_PAGESIZE * pool->page_count)) < (ptr + (ARCH_PAGESIZE * page_count))) {
        goto die;
    }
    size_t block_size = 1;
    while (block_size < page_count) {
        if ((SIZE_MAX / 2) < block_size) {
            goto die;
        }
        block_size *= 2;
    }
    if (pool->page_count < block_size) {
        goto die;
    }

    size_t current_block_index = (ptr - pool->baseaddr) / (block_size * ARCH_PAGESIZE);
    size_t currentlevel = blocksize_to_pagepool_level(pool, block_size);
    assert(((ptr - pool->baseaddr) % (block_size * ARCH_PAGESIZE)) == 0);
    while (1) {
        // Mark it as available
        long bit_index = bit_index_for_pagepool_block(currentlevel, current_block_index);
        if (bitmap_is_bit_set(&pool->bitmap, bit_index)) {
            co_printf("double free detected\n");
            goto die;
        }

        bitmap_set_bit(&pool->bitmap, bit_index);
        if (currentlevel == 0) {
            // No lower levels
            break;
        }
        // See if neighbor block is also available
        long neighbor_bit_index = 0;
        if ((current_block_index % 2) == 0) {
            neighbor_bit_index = bit_index + 1;
        } else {
            neighbor_bit_index = bit_index - 1;
        }
        if (!bitmap_is_bit_set(&pool->bitmap, neighbor_bit_index)) {
            // Neighbor is in use; No further action is needed.
            break;
        }
        // Combine with neighbor block, and move to lower level.
        bitmap_clear_bit(&pool->bitmap, bit_index);
        bitmap_clear_bit(&pool->bitmap, neighbor_bit_index);
        currentlevel--;
        current_block_index /= 2;
    }
    return;
die:
    panic("pmm: bad free");
}

NODISCARD static bool test_pagepool_alloc(struct pagepool *pool, size_t alloc_size, size_t alloc_count, size_t page_count) {
    PHYSPTR expected_ptr = pool->baseaddr;
    for (size_t i = 0; i < alloc_count; i++, expected_ptr += alloc_size) {
        size_t result_page_count = page_count;
        PHYSPTR allocptr = alloc_from_pool(pool, &result_page_count);
        if (allocptr == PHYSICALPTR_NULL) {
            co_printf("could not allocate pages(allocation %zu, page count %zu)\n", i, page_count);
            return false;
        }
        if (result_page_count != page_count) {
            co_printf("expected %zu pages, got %zu pages(allocation %zu)\n", page_count, result_page_count, i);
            return false;
        }
        if (expected_ptr != allocptr) {
            co_printf("expected address %p pages, got %p(allocation %zu)\n", expected_ptr, allocptr, i);
            return false;
        }
    }
    return true;
}

static void test_pagepool_fill(struct pagepool *pool, size_t alloc_size, size_t alloc_count) {
    size_t test_ptr_count = alloc_size / sizeof(PHYSPTR);
    PHYSPTR alloc_ptr = pool->baseaddr;
    for (size_t i = 0; i < alloc_count; i++, alloc_ptr += alloc_size) {
        for (size_t j = 0; j < test_ptr_count; j++) {
            PHYSPTR dest_addr = alloc_ptr + (sizeof(PHYSPTR) * j);
            STATIC_ASSERT_TEST(sizeof(UINT) == sizeof(uint32_t));
            ppoke32(dest_addr, dest_addr, false);
        }
    }
}

NODISCARD static bool test_pagepool_compare(struct pagepool *pool, size_t alloc_size, size_t alloc_count) {
    size_t test_ptr_count = alloc_size / sizeof(PHYSPTR);
    PHYSPTR alloc_ptr = pool->baseaddr;
    for (size_t i = 0; i < alloc_count; i++, alloc_ptr += alloc_size) {
        for (size_t j = 0; j < test_ptr_count; j++) {
            PHYSPTR srcaddr = alloc_ptr + (sizeof(PHYSPTR) * j);
            PHYSPTR expectedvalue = srcaddr;
            PHYSPTR gotvalue = ppeek32(srcaddr, false);
            if (expectedvalue != gotvalue) {
                co_printf("value mismatch at %p(allocation %zu, base %p, offset %zu): expected %p, got %p\n", srcaddr, i, alloc_ptr, j, expectedvalue, gotvalue);
                return false;
            }
        }
    }
    return true;
}

static void test_pagepool_free(struct pagepool *pool, size_t alloc_size, size_t alloc_count, size_t page_count) {
    PHYSPTR alloc_ptr = pool->baseaddr;
    for (size_t i = 0; i < alloc_count; i++, alloc_ptr += alloc_size) {
        free_from_pool(pool, alloc_ptr, page_count);
    }
}

static bool test_pagepool(struct pagepool *pool) {
    size_t currentlevel = 0;
    size_t current_page_count = pool->page_count;
    size_t current_alloc_count = 1;
    while (1) {
        if (current_page_count == 0) {
            break;
        }
        size_t current_alloc_size = current_page_count * ARCH_PAGESIZE;
        if (!test_pagepool_alloc(pool, current_alloc_size, current_alloc_count, current_page_count)) {
            goto testfail;
        }
        test_pagepool_fill(pool, current_alloc_size, current_alloc_count);
        if (!test_pagepool_compare(pool, current_alloc_size, current_alloc_count)) {
            goto testfail;
        }
        test_pagepool_free(pool, current_alloc_size, current_alloc_count, current_page_count);
        currentlevel++;
        current_page_count /= 2;
        current_alloc_count *= 2;
        continue;
    testfail:
        co_printf("-               level: %zu\n", currentlevel);
        co_printf("-       current_level: %zu\n", currentlevel);
        co_printf("-  current_page_count: %zu\n", current_page_count);
        co_printf("- current_alloc_count: %zu\n", current_alloc_count);
        co_printf("-  current_alloc_size: %zu\n", current_alloc_size);
        co_printf("pmm: sequential test failed\n");
        return false;
    }
    return true;
}

// TODO: Add this to the PMM test code
void pmm_test_pagepools(void) {
    for (struct pagepool *pool = s_firstpool; pool != NULL; pool = pool->nextpool) {
        test_pagepool(pool);
    }
}

void pmm_register(PHYSPTR base, size_t page_count) {
    assert(base != 0);

    uintptr_t current_baseaddress = base;
    size_t remaining_pagecount = page_count;
    while (remaining_pagecount != 0) {
        size_t poolpagecount = 0;
        size_t levelcount = 0;
        size_t bitcount = 0;
        calculate_pagepoolsizes(&poolpagecount, &levelcount, &bitcount, remaining_pagecount);
        size_t wordcount = bitmap_needed_word_count(bitcount);
        size_t bitmapsize = wordcount * sizeof(UINT);
        size_t metadatasize = bitmapsize + sizeof(struct pagepool);
        struct pagepool *pool = heap_alloc(metadatasize, HEAP_FLAG_ZEROMEMORY);
        if (pool == NULL) {
            co_printf("pmm: unable to alloate metadata memory for managing %d pages\n", poolpagecount);
            continue;
        }
        if (CONFIG_PRINT_POOL_INIT) {
            co_printf("pmm: initializing %zuk pool at %#x\n", (poolpagecount * ARCH_PAGESIZE) / 1024, current_baseaddress);
        }
        pool->nextpool = s_firstpool;
        pool->bitmap.words = pool->bitmapdata;
        pool->bitmap.wordcount = wordcount;
        pool->baseaddr = current_baseaddress;
        assert((pool->baseaddr % ARCH_PAGESIZE) == 0);
        pool->page_count = poolpagecount;
        // Mark first level as available.
        bitmap_set_bit(&pool->bitmap, 0);
        remaining_pagecount -= poolpagecount;
        if (CONFIG_TEST_POOL) {
            co_printf("pmm: testing the new page pool at %#lx\n", current_baseaddress);
            if (!test_pagepool(pool)) {
                panic("pmm: page pool test failed");
            }
        }
        s_firstpool = pool;
        current_baseaddress += ARCH_PAGESIZE * poolpagecount;
    }
}

PHYSPTR pmm_alloc(size_t *pagecount_inout) {
    assert(*pagecount_inout != 0);
    bool prev_interrupts = arch_interrupts_disable();
    PHYSPTR result = PHYSICALPTR_NULL;
    for (struct pagepool *pool = s_firstpool; pool != NULL; pool = pool->nextpool) {
        size_t newpagecount = *pagecount_inout;
        result = alloc_from_pool(pool, &newpagecount);
        if (result != PHYSICALPTR_NULL) {
            *pagecount_inout = newpagecount;
            break;
        }
    }
    interrupts_restore(prev_interrupts);
    return result;
}

void pmm_free(PHYSPTR ptr, size_t page_count) {
    if ((ptr == 0) || (page_count == 0)) {
        return;
    }
    bool prev_interrupts = arch_interrupts_disable();
    for (struct pagepool *pool = s_firstpool; pool != NULL; pool = pool->nextpool) {
        uintptr_t pool_data_start = pool->baseaddr;
        uintptr_t pool_data_end = pool_data_start + (ARCH_PAGESIZE * pool->page_count - 1);
        if ((ptr < pool_data_start) || (pool_data_end < ptr)) {
            continue;
        }
        uintptr_t end = ptr + (ARCH_PAGESIZE * page_count - 1);
        if ((end <= pool_data_start) || (pool_data_end < end)) {
            goto badptr;
        }
        free_from_pool(pool, ptr, page_count);
        interrupts_restore(prev_interrupts);
        return;
    }
badptr:
    panic("pmm: bad pointer");
}

size_t pmm_get_totalmem(void) {
    size_t page_count = 0;
    for (struct pagepool *pool = s_firstpool; pool != NULL; pool = pool->nextpool) {
        page_count += pool->page_count;
    }
    assert(page_count <= (SIZE_MAX / ARCH_PAGESIZE));
    return page_count * ARCH_PAGESIZE;
}

////////////////////////////////////////////////////////////////////////////////

#define RAND_TEST_ALLOC_COUNT 10

bool pmm_pagepool_test_random(void) {
    size_t allocsizes[RAND_TEST_ALLOC_COUNT];
    PHYSPTR allocptrs[RAND_TEST_ALLOC_COUNT];

    size_t maxpagecount = 0;
    for (struct pagepool *pool = s_firstpool; pool != NULL; pool = pool->nextpool) {
        if (maxpagecount < pool->page_count) {
            maxpagecount = pool->page_count;
        }
    }
    maxpagecount /= RAND_TEST_ALLOC_COUNT;
    if (maxpagecount == 0) {
        maxpagecount = 1;
    }

    for (size_t i = 0; i < RAND_TEST_ALLOC_COUNT; i++) {
        while (1) {
            allocsizes[i] = rand() % maxpagecount;
            allocptrs[i] = pmm_alloc(&allocsizes[i]);
            if (allocptrs[i] != PHYSICALPTR_NULL) {
                break;
            }
        }
    }
    for (size_t i = 0; i < RAND_TEST_ALLOC_COUNT; i++) {
        size_t testptrcount = (allocsizes[i] * ARCH_PAGESIZE) / sizeof(void *);
        for (size_t j = 0; j < testptrcount; j++) {
            PHYSPTR destaddr = allocptrs[i] + sizeof(PHYSPTR) * 4;
            STATIC_ASSERT_TEST(sizeof(PHYSPTR) == sizeof(uint32_t));
            ppoke32(destaddr, destaddr, false);
        }
    }
    for (size_t i = 0; i < RAND_TEST_ALLOC_COUNT; i++) {
        size_t testptrcount = (allocsizes[i] * ARCH_PAGESIZE) / sizeof(void *);
        for (size_t j = 0; j < testptrcount; j++) {
            PHYSPTR srcaddr = allocptrs[i] + sizeof(PHYSPTR) * 4;
            PHYSPTR expectedvalue = srcaddr;
            PHYSPTR gotvalue = ppeek32(srcaddr, false);
            if (gotvalue != expectedvalue) {
                co_printf("value mismatch at %p(allocation %zu, base %p, offset %zu): expected %p, got %p\n", srcaddr, i, allocptrs[i], j, expectedvalue, gotvalue);
                goto testfail;
            }
        }
    }
    for (size_t i = 0; i < RAND_TEST_ALLOC_COUNT; i++) {
        pmm_free(allocptrs[i], allocsizes[i]);
    }
    return true;
testfail:
    co_printf("pmm: random test failed\n");
    return false;
}
