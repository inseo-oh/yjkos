#include <assert.h>
#include <kernel/arch/interrupts.h>
#include <kernel/arch/mmu.h>
#include <kernel/io/tty.h>
#include <kernel/lib/bitmap.h>
#include <kernel/lib/diagnostics.h>
#include <kernel/lib/miscmath.h>
#include <kernel/lib/pstring.h>
#include <kernel/mem/heap.h>
#include <kernel/mem/pmm.h>
#include <kernel/panic.h>
#include <kernel/status.h>
#include <kernel/types.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

//------------------------------- Configuration -------------------------------

// Print when a pool initializes?
static bool const CONFIG_PRINT_POOL_INIT = false;
// Should we run sequential allocation test when a pool initializes?
// This can take a *very* long time depending on CPU speed and how large the pool is.
static bool const CONFIG_TEST_POOL = false;

//-----------------------------------------------------------------------------

struct pagepool {
    struct pagepool *nextpool;
    struct bitmap bitmap;
    physptr baseaddr;
    size_t pagecount;
    uint8_t levelcount;
    uint bitmapdata[];
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

static void calculate_pagepoolsizes(size_t *pagecount_out, size_t *levelcount_out, size_t *bitcount_out, size_t pagecount) {
    size_t currentpagecount = pagecount;
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

static void bitindicesrange_for_level(long *start_out, long *end_out, size_t level) {
    size_t blockinlevel = 1;
    size_t currentlevel = 0;
    size_t bitoffset = 0;
    while (currentlevel < level) {
        bitoffset += blockinlevel;
        blockinlevel *= 2;
        currentlevel++;
    }
    *start_out = bitoffset;
    *end_out = bitoffset + blockinlevel - 1;
}

static long bitindex_for_pagepoolblock(size_t level, size_t block) {
    long start, end;
    bitindicesrange_for_level(&start, &end, level);
    size_t blocksinlevel = end - start + 1;
    assert(block < blocksinlevel);
    return start + block;
}

static size_t blocksize_to_pagepoollevel(struct pagepool const *pool, size_t size) {
    size_t sizeperblock = pool->pagecount;
    size_t currentlevel = 0;
    while (size < sizeperblock) {
        currentlevel++;
        sizeperblock /= 2;
    }
    return currentlevel;
}


static FAILABLE_FUNCTION allocfrompool(physptr *addr_out, struct pagepool *pool, size_t *pagecount_inout) {
FAILABLE_PROLOGUE
    if (*pagecount_inout == 0) {
        THROW(ERR_NOMEM);
    }
    // Adjust page size to nearest 2^n
    size_t blocksize = 1;
    while (blocksize < *pagecount_inout) {
        if ((SIZE_MAX / 2) < blocksize) {
            THROW(ERR_NOMEM);
        }
        blocksize *= 2;
    }
    if (pool->pagecount < blocksize) {
        THROW(ERR_NOMEM);
    }
    *pagecount_inout = blocksize;
    size_t wantedlevel = blocksize_to_pagepoollevel(pool, blocksize);
    size_t foundlevel = wantedlevel;
    size_t foundblockindex;
    while(1) {
        long bitstart, bitend;
        bitindicesrange_for_level(&bitstart, &bitend, foundlevel);
        long foundat = bitmap_findsetbits(&pool->bitmap, bitstart, 1);
        if (0 <= foundat && foundat <= bitend) {
            // Found block
            foundblockindex = foundat - bitstart;
            break;
        }
        if (foundlevel == 0) {
            THROW(ERR_NOMEM);
        }
        foundlevel--;
    }
    // If we found block at lower level, split blocks until we reach there.
    size_t currentblockindex = foundblockindex;
    for (size_t currentlevel = foundlevel; currentlevel < wantedlevel; currentlevel++, currentblockindex *= 2) {
        // Mark current one as unavilable.
        long bitindex = bitindex_for_pagepoolblock(currentlevel, currentblockindex);
        bitmap_clearbit(&pool->bitmap, bitindex);
        // Mark upper block's second block as available.
        // (First block will be marked as unavailable next time, so don't touch that)
        bitindex = bitindex_for_pagepoolblock(currentlevel + 1, (currentblockindex * 2) + 1);
        bitmap_setbit(&pool->bitmap, bitindex);
    }
    // Mark resulting block as unavailable
    long bitindex = bitindex_for_pagepoolblock(wantedlevel, currentblockindex);
    bitmap_clearbit(&pool->bitmap,bitindex);

    *addr_out = pool->baseaddr + (blocksize * currentblockindex * ARCH_PAGESIZE);
FAILABLE_EPILOGUE_BEGIN 
FAILABLE_EPILOGUE_END 
}

static void freefrompool(struct pagepool *pool, physptr ptr, size_t pagecount) {
    if (ptr == 0) {
        return;
    }
    if((ptr < pool->baseaddr) || (pool->baseaddr + (ARCH_PAGESIZE * pool->pagecount)) < (ptr + (ARCH_PAGESIZE * pagecount))) {
        goto die;
    }
    size_t blocksize = 1;
    while (blocksize < pagecount) {
       if ((SIZE_MAX / 2) < blocksize) {
            goto die;
        }
        blocksize *= 2;
    }
    if (pool->pagecount < blocksize) {
        goto die;
    }

    size_t currentblockindex = (ptr - pool->baseaddr) / (blocksize * ARCH_PAGESIZE);
    size_t currentlevel = blocksize_to_pagepoollevel(pool, blocksize);
    assert(((ptr - pool->baseaddr) % (blocksize * ARCH_PAGESIZE)) == 0);
    while (1) {
        // Mark it as available
        long bitindex = bitindex_for_pagepoolblock(currentlevel, currentblockindex);
        if (bitmap_isbitset(&pool->bitmap, bitindex)) {
            tty_printf("double free detected\n");
            goto die;
        }

        bitmap_setbit(&pool->bitmap, bitindex);
        if (currentlevel == 0) {
            // No lower levels
            break;
        }
        // See if neighbor block is also available
        size_t neighborbitindex;
        if ((currentblockindex % 2) == 0) {
            neighborbitindex = bitindex + 1;
        } else {
            neighborbitindex = bitindex - 1;
        }
        if (!bitmap_isbitset(&pool->bitmap, neighborbitindex)) {
            // Neighbor is in use; No further action is needed.
            break;
        }
        // Combine with neighbor block, and move to lower level.
        bitmap_clearbit(&pool->bitmap,bitindex);
        bitmap_clearbit(&pool->bitmap,neighborbitindex);
        currentlevel--;
        currentblockindex /= 2;
    }
    return;
die:
    panic("pmm: bad free");
}

static bool testpagepool(struct pagepool *pool) {
    size_t currentlevel = 0;
    size_t currentpagecount = pool->pagecount;
    size_t currentalloccount = 1;
    while(1) {
        if (currentpagecount == 0) {
            break;
        }
        size_t currentallocsize = currentpagecount * ARCH_PAGESIZE;
        size_t testptrcount = currentallocsize / sizeof(physptr); 
        physptr expectedptr = pool->baseaddr;
        for (size_t i = 0; i < currentalloccount; i++, expectedptr += currentallocsize) {
            size_t resultpagecount = currentpagecount;
            physptr allocptr;
            status_t status = allocfrompool(&allocptr, pool, &resultpagecount);
            if (status != OK) {
                tty_printf("could not allocate pages(allocation %zu, page count %zu)\n", i, currentpagecount);
                goto testfail;
            }
            if (resultpagecount != currentpagecount) {
                tty_printf("expected %zu pages, got %zu pages(allocation %zu)\n", currentpagecount, resultpagecount, i);
                goto testfail;
            }
            if (expectedptr != allocptr) {
                tty_printf("expected address %p pages, got %p(allocation %zu)\n", expectedptr, allocptr, i);
                goto testfail;
            }
        }
        physptr allocptr = pool->baseaddr;
        for (size_t i = 0; i < currentalloccount; i++, allocptr += currentallocsize) {
            for (size_t j = 0; j < testptrcount; j++) {
                physptr dest_addr = allocptr + (sizeof(physptr) * j);
                STATIC_ASSERT_TEST(sizeof(uint) == sizeof(uint32_t));
                ppoke32(dest_addr, dest_addr, false);
            }
        }
        allocptr = pool->baseaddr;
        for (size_t i = 0; i < currentalloccount; i++, allocptr += currentallocsize) {
            for (size_t j = 0; j < testptrcount; j++) {
                physptr srcaddr = allocptr + (sizeof(physptr) * j);
                physptr expectedvalue = srcaddr;
                physptr gotvalue = ppeek32(srcaddr, false);
                if (expectedvalue  != gotvalue) {
                    tty_printf("value mismatch at %p(allocation %zu, base %p, offset %zu): expected %p, got %p\n", srcaddr, i, allocptr, j, expectedvalue, gotvalue);
                    goto testfail;
                }
            }
        }
        allocptr = pool->baseaddr;
        for (size_t i = 0; i < currentalloccount; i++, allocptr += currentallocsize) {
            freefrompool(pool, allocptr, currentpagecount);
        }
        currentlevel++;
        currentpagecount /= 2;
        currentalloccount *= 2;
        continue;
    testfail:
        tty_printf("-               level: %zu\n", currentlevel);
        tty_printf("-       current_level: %zu\n", currentlevel);
        tty_printf("-  current_page_count: %zu\n", currentpagecount);
        tty_printf("- current_alloc_count: %zu\n", currentalloccount);
        tty_printf("-  current_alloc_size: %zu\n", currentallocsize);
        tty_printf("pmm: sequential test failed\n");
        return false;
    }
    return true;
}

void pmm_testpagepools(void) {
    for (struct pagepool *pool = s_firstpool; pool != NULL; pool = pool->nextpool) {
        testpagepool(pool);
    }
}

void pmm_register(physptr base, size_t pagecount) {
    assert(base != 0);

    uintptr_t current_baseaddress = base;
    size_t remaining_pagecount = pagecount;
    while(remaining_pagecount != 0) {
        size_t poolpagecount = 0, levelcount = 0, bitcount = 0;
        calculate_pagepoolsizes(&poolpagecount, &levelcount, &bitcount, remaining_pagecount);
        size_t wordcount = bitmap_neededwordcount(bitcount);
        size_t bitmapsize = wordcount * sizeof(uint);
        size_t metadatasize = bitmapsize + sizeof(struct pagepool);
        struct pagepool *pool = heap_alloc(metadatasize, HEAP_FLAG_ZEROMEMORY);
        if (pool == NULL) {
            tty_printf("pmm: unable to alloate metadata memory for managing %d pages\n", poolpagecount);
            continue;
        }
        if (CONFIG_PRINT_POOL_INIT) {
            tty_printf("pmm: initializing %zuk pool at %#x\n", (poolpagecount * ARCH_PAGESIZE) / 1024, current_baseaddress);
        }
        pool->nextpool = s_firstpool;
        pool->bitmap.words = pool->bitmapdata;
        pool->bitmap.wordcount = wordcount;
        pool->baseaddr = current_baseaddress;
        assert((pool->baseaddr % ARCH_PAGESIZE) == 0);
        pool->pagecount = poolpagecount;
        // Mark first level as available.
        bitmap_setbit(&pool->bitmap, 0);
        remaining_pagecount -= poolpagecount;
        if (CONFIG_TEST_POOL) {
            tty_printf("pmm: testing the new page pool at %p\n", (void *)current_baseaddress);
            if (!testpagepool(pool)) {
                panic("pmm: page pool test failed");
            }
        }
        s_firstpool = pool;
        current_baseaddress += ARCH_PAGESIZE * poolpagecount;
    }
}

FAILABLE_FUNCTION pmm_alloc(physptr *ptr_out, size_t *pagecount_inout) {
FAILABLE_PROLOGUE
    bool previnterrupts = arch_interrupts_disable();
    if (*pagecount_inout == 0) {
        THROW(ERR_NOMEM);
    }
    bool ok = false;
    for (struct pagepool *pool = s_firstpool; pool != NULL; pool = pool->nextpool) {
        size_t newpagecount = *pagecount_inout;
        status_t status = allocfrompool(ptr_out, pool, &newpagecount);
        if (status == OK) {
            *pagecount_inout = newpagecount;
            ok = true;
            break;
        }
    }
    if (!ok) {
        THROW(ERR_NOMEM);
    }
FAILABLE_EPILOGUE_BEGIN
    interrupts_restore(previnterrupts);
FAILABLE_EPILOGUE_END
}

void pmm_free(physptr ptr, size_t pagecount) {
    if ((ptr == 0) || (pagecount == 0)) {
        return;
    }
    bool previnterrupts = arch_interrupts_disable();
    for (struct pagepool *pool = s_firstpool; pool != NULL; pool = pool->nextpool) {
        uintptr_t pooldatastart = pool->baseaddr;
        uintptr_t pooldataend = pooldatastart + (ARCH_PAGESIZE * pool->pagecount - 1);
        if ((ptr < pooldatastart) || (pooldataend < ptr)) {
            continue;
        }
        uintptr_t end = ptr + (ARCH_PAGESIZE * pagecount - 1);
        if ((end <= pooldatastart) || (pooldataend < end)) {
            goto badptr;
        }
        freefrompool(pool, ptr, pagecount);
        interrupts_restore(previnterrupts);
        return;
    }
badptr:
    panic("pmm: bad pointer");
}

size_t pmm_get_totalmem(void) {
    size_t pagecount = 0;
    for (struct pagepool *pool = s_firstpool; pool != NULL; pool = pool->nextpool) {
        pagecount += pool->pagecount;
    }
    assert(pagecount <= (SIZE_MAX / ARCH_PAGESIZE));
    return pagecount * ARCH_PAGESIZE;
}

////////////////////////////////////////////////////////////////////////////////

enum {
    RAND_TEST_ALLOC_COUNT = 10,
};

bool pmm_pagepool_test_random(void) {
    size_t allocsizes[RAND_TEST_ALLOC_COUNT];
    physptr allocptrs[RAND_TEST_ALLOC_COUNT];

    size_t maxpagecount = 0;
    for (struct pagepool *pool = s_firstpool; pool != NULL; pool = pool->nextpool) {
        if (maxpagecount < pool->pagecount) { 
            maxpagecount = pool->pagecount;
        }
    }
    maxpagecount /= RAND_TEST_ALLOC_COUNT;
    if (maxpagecount == 0) {
        maxpagecount = 1;
    }

    for (size_t i = 0; i < RAND_TEST_ALLOC_COUNT; i++) {
        while(1) {
            allocsizes[i] = rand() % maxpagecount;
            status_t status = pmm_alloc(&allocptrs[i], &allocsizes[i]);
            if (status == OK) {
                break;
            }
        }
    }
    for (size_t i = 0; i < RAND_TEST_ALLOC_COUNT; i++) {
        size_t testptrcount = (allocsizes[i] * ARCH_PAGESIZE) / sizeof(void *); 
        for (size_t j = 0; j < testptrcount; j++) {
            physptr destaddr = allocptrs[i] + sizeof(physptr) * 4;
            STATIC_ASSERT_TEST(sizeof(physptr) == sizeof(uint32_t));
            ppoke32(destaddr, destaddr, false);
        }
    }
    for (size_t i = 0; i < RAND_TEST_ALLOC_COUNT; i++) {
        size_t testptrcount = (allocsizes[i] * ARCH_PAGESIZE) / sizeof(void *); 
        for (size_t j = 0; j < testptrcount; j++) {
            physptr srcaddr = allocptrs[i] + sizeof(physptr) * 4;
            physptr expectedvalue = srcaddr;
            physptr gotvalue = ppeek32(srcaddr, false);
            if (gotvalue != expectedvalue) {
                tty_printf("value mismatch at %p(allocation %zu, base %p, offset %zu): expected %p, got %p\n", srcaddr, i, allocptrs[i], j, expectedvalue, gotvalue);
                goto testfail;
            }
        }
    }
    for (size_t i = 0; i < RAND_TEST_ALLOC_COUNT; i++) {
        pmm_free(allocptrs[i], allocsizes[i]);
    }
    return true;
testfail:
    tty_printf("pmm: random test failed\n");
    return false;
}

