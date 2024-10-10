#include <kernel/arch/interrupts.h>
#include <kernel/arch/mmu.h>
#include <kernel/io/tty.h>
#include <kernel/lib/bitmap.h>
#include <kernel/lib/diagnostics.h>
#include <kernel/lib/list.h>
#include <kernel/lib/miscmath.h>
#include <kernel/mem/heap.h>
#include <kernel/mem/pmm.h>
#include <kernel/mem/vmm.h>
#include <kernel/panic.h>
#include <kernel/status.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

//------------------------------- Configuration -------------------------------

// Should we run sequential allocation test when a heap pool initializes?
// This can take a *very* long time depending on CPU speed and how large the pool is.
static bool const CONFIG_DO_POOL_SEQUENTIAL_TEST = false; 
// Should sequential test be verbose?
static bool const CONFIG_SEQUENTIAL_TEST_VERBOSE = true;

//-----------------------------------------------------------------------------

static uint8_t const POISONVALUES[] = {
    0xe9, 0x29, 0xf3, 0xfb, 0xd7, 0x67, 0xaa, 0x5a
};

typedef struct poolheader poolheader_t;
struct poolheader {
    max_align_t *blockpool;
    list_node_t node;
    bitmap_t blockbitmap;
    size_t blockcount;
    size_t usedblockcount;
    size_t pagecount;
    max_align_t heapdata[];
};

typedef struct allocheader allocheader_t;
struct allocheader {
    list_node_t node;
    poolheader_t *pool;
    size_t blockcount, size;
    max_align_t data[];
};

static size_t const BLOCK_SIZE = 32;

static size_t s_freeblockcount = 0;
static list_t s_heappoollist; // poolheader_t items
static list_t s_alloclist;    // allocheader_t items
static bool s_initialheapinitialized = false;

static uint8_t s_initialheapmemory[1024*1024*2];

STATIC_ASSERT_TEST(alignof(poolheader_t) == alignof(max_align_t));

static size_t bytecount_for_blockcount(size_t blockcount) {
    return (BLOCK_SIZE * blockcount) - sizeof(allocheader_t) - sizeof(POISONVALUES);
}

static size_t actualallocsize(size_t size) {
    return size + sizeof(allocheader_t) + sizeof(POISONVALUES);
}

static void checkpoision(void) {
    bool die = false;
    for (list_node_t *allocnode = s_alloclist.front; allocnode != NULL; allocnode = allocnode->next) {
        allocheader_t *alloc = allocnode->data;
        uint8_t *poision = &((uint8_t *)alloc->data)[alloc->size];
        for (size_t i = 0; i < sizeof(POISONVALUES); i++) {
            if (poision[i] != POISONVALUES[i]) {
                tty_printf("[alloc@%p] poision value check failed at offset %zu -> Expected %02x, got %02x\n", alloc, i, POISONVALUES[i], poision[i]);
                die = true;
            }
        }
    }
    if (die) {
        panic("heap overflow detected");
    }
}

static void *allocfrompool(poolheader_t *self, size_t size) {
    ASSERT_INTERRUPTS_DISABLED();
    if (size == 0) {
        return NULL;
    }
    allocheader_t *alloc = NULL;
    if ((SIZE_MAX - sizeof(allocheader_t)) < size) {
        return NULL;
    }
    size_t actualsize = actualallocsize(size);
    size_t blockcount = sizetoblocks(actualsize, BLOCK_SIZE);
    bitindex_t blockindex = bitmap_findsetbits(&self->blockbitmap, 0, blockcount);
    if (blockindex < 0) {
        goto out;
    }
    for (size_t i = 0; i < blockcount; i++) {
        assert(bitmap_isbitset(&self->blockbitmap, blockindex + i));
    }
    bitmap_clearbits(&self->blockbitmap, blockindex, blockcount);
    for (size_t i = 0; i < blockcount; i++) {
        assert(!bitmap_isbitset(&self->blockbitmap, blockindex + i));
    }
    uintptr_t offsetinpool = blockindex * BLOCK_SIZE;
    assert(isaligned(offsetinpool, alignof(max_align_t)));
    alloc = (allocheader_t *)((uintptr_t)self->blockpool + offsetinpool);
    self->usedblockcount += blockcount;
out:
    if (alloc == NULL) {
        return NULL;
    }
    alloc->pool = self;
    alloc->blockcount = blockcount;
    alloc->size = size;
    assert(blockcount <= s_freeblockcount);
    s_freeblockcount -= blockcount;
    memset(alloc->data, 0x90, size);
    // Setup poision values
    uint8_t *poisiondest = &((uint8_t *)alloc->data)[size];
    for (size_t i = 0; i < sizeof(POISONVALUES); i++) {
        poisiondest[i] = POISONVALUES[i];
    }
    list_insertback(&s_alloclist, &alloc->node, alloc);
    checkpoision();
    return alloc->data;
}

static allocheader_t *allocheaderof(void *ptr) {
    uintptr_t addr = (uintptr_t)ptr;
    if ((ptr == NULL) || (!isaligned(addr, alignof(max_align_t))) || (addr < offsetof(allocheader_t, data))) {
        return NULL;
    }
    return (allocheader_t *)(addr - offsetof(allocheader_t, data));
}

static bool testheap(poolheader_t *self) {
    size_t allocblockcount = 1;
    uintptr_t poolstartaddr = (uintptr_t)self->blockpool;
    uintptr_t poolendaddr = poolstartaddr + ((BLOCK_SIZE * self->blockcount) - 1);
    tty_printf("sequential memory test start(%p~%p, size %zu)\n", (void *)poolstartaddr, (void *)(poolendaddr - 1), poolendaddr - poolstartaddr);

    while(1) {
        size_t bytecount = bytecount_for_blockcount(allocblockcount);
        size_t alloccount = self->blockcount / allocblockcount;
        if (alloccount == 0) {
            break;
        }
        if (CONFIG_SEQUENTIAL_TEST_VERBOSE) {
            tty_printf("* Testing %u x %u blocks: ", alloccount, allocblockcount);
        }
        assert(bytecount != 0);

        // We run the basic allocation checks twice, to see if filling the returned memory overwrote crucial data structures.
        for (int type = 0; type < 2; type++) {
            uintptr_t expectedblockaddr = (uintptr_t)self->blockpool + sizeof(allocheader_t);
            if (CONFIG_SEQUENTIAL_TEST_VERBOSE) {
                if (type == 0) {
                    tty_printf("[allocate&fill]");
                } else {
                    tty_printf(" [basic re-check]");
                }
            }
            for (size_t i = 0; i < alloccount; i++) {
                uint8_t *alloc;
                if (type == 0) {
                    alloc = allocfrompool(self, bytecount);
                    if (expectedblockaddr != (uintptr_t)alloc) {
                        arch_interrupts_disable();
                        tty_printf("unexpected address\n");
                        goto testfailed2;
                    }
                } else {
                    alloc = (uint8_t *)expectedblockaddr;
                }
                if(!isaligned((uintptr_t)alloc, alignof(max_align_t))) {
                    arch_interrupts_disable();
                    tty_printf("misaligned allocation\n");
                    goto testfailed2;
                }

                if (poolendaddr < (uintptr_t)alloc) {
                    arch_interrupts_disable();
                    tty_printf("address beyond end of the heap\n");
                    goto testfailed2;
                }
                allocheader_t *allocheader = allocheaderof(alloc);
                if (allocheader->pool != self) {
                    arch_interrupts_disable();
                    tty_printf("bad pool pointer\n");
                    goto testfailed2_withallocheader;
                }
                if (allocheader->blockcount != allocblockcount) {
                    arch_interrupts_disable();
                    tty_printf("incorrect block count\n");
                    goto testfailed2_withallocheader;
                }
                if (allocheader->data != (void *)alloc) {
                    arch_interrupts_disable();
                    tty_printf("incorrect data start\n");
                    goto testfailed2_withallocheader;
                }
                // If it's not type 0, we already overwrote memory with other values, so don't check for initial pattern.
                if (type == 0 && (*((uint32_t *)alloc) != 0x90909090)) {
                    arch_interrupts_disable();
                    tty_printf("incorrect initial pattern (got %p)\n", *((uint32_t *)alloc));
                    goto testfailed2_withallocheader;
                }
                if (type == 0) {
                    for (size_t j = 0; j < bytecount; j++) {
                        alloc[j] = i;
                    }
                }
                expectedblockaddr += BLOCK_SIZE * allocblockcount;
                continue;
            testfailed2_withallocheader:
                tty_printf(" - alloc header:\n");
                tty_printf(" +-- region pointer: %p\n", allocheader->pool);
                tty_printf(" +-- block count:    %u\n", allocheader->blockcount);
                tty_printf(" +-- data start:     %p\n", allocheader->data);
            testfailed2:
                tty_printf("- alloc pointer:     %p\n", (void *)alloc);
                tty_printf("- expected pointer:  %p\n", (void *)expectedblockaddr);
                tty_printf("- alloc index:       %u/%u\n", i, (alloccount - 1));
                goto testfailed1;    
            }
        }

        if (CONFIG_SEQUENTIAL_TEST_VERBOSE) {
            tty_printf(" [compare]");
        }
        uintptr_t blockaddr = (uintptr_t)self->blockpool + sizeof(allocheader_t);
        for (size_t i = 0; i < alloccount; i++) {
            uint8_t *alloc = (uint8_t *)blockaddr;
            for (size_t j = 0; j < bytecount; j++) {
                if (alloc[j] != (i & 0xff)) {
                    tty_printf("corrupted data\n");
                    tty_printf("- allocated at:      %p\n", (void *)blockaddr);
                    tty_printf("- alloc index:       %u/%u\n", i, (alloccount - 1));
                    tty_printf("- byte offset:       %u/%u\n", j, bytecount - 1);
                    tty_printf("- expected:          %u\n", i);
                    tty_printf("- got:               %u\n", alloc[j]);
                    goto testfailed1;
                }
            }
            blockaddr += BLOCK_SIZE * allocblockcount;
        }

        if (CONFIG_SEQUENTIAL_TEST_VERBOSE) {
            tty_printf(" [free]");
        }
        blockaddr = (uintptr_t)self->blockpool + sizeof(allocheader_t);
        for (size_t i = 0; i < alloccount; i++) {
            uint8_t *alloc = (uint8_t *)blockaddr;
            heap_free(alloc);
            for (size_t j = 0; j < bytecount; j++) {
                if (alloc[j] != 0x6f) {
                    tty_printf("free pattern(0x6f) not found\n");
                    tty_printf("- allocated at:      %p\n", (void *)blockaddr);
                    tty_printf("- alloc index:       %u/%u\n", i, (alloccount - 1));
                    tty_printf("- byte offset:       %u/%u\n", j, bytecount - 1);
                    tty_printf("- got:               %u\n", alloc[j]);
                    goto testfailed1;
                }
            }
            blockaddr += BLOCK_SIZE * allocblockcount;
        }
        allocblockcount++;
        tty_printf(" -> OK!\n");
        continue;
    testfailed1:
        tty_printf("- alloc block count: %u\n", allocblockcount);
        tty_printf("- alloc byte count:  %u\n", bytecount);
        tty_printf("- pool start addr:   %p\n", (void *)poolstartaddr);
        tty_printf("- pool end addr:     %p\n", (void *)poolendaddr);
        return false;
    }
    tty_printf("sequential memory test ok(%p~%p)\n", (void *)poolstartaddr, (void *)(poolendaddr - 1));
    return true;
}

// Backup of old code for implementing userspace malloc in the future. It will no longer work as-is though.
// (It allocates new heap based on desired size)
#if 0
static poolheader_t *createpool(size_t minmemsize) {
    size_t desiredblockcount;
    // Note that desiredblockcount works a bit different depending on whether it's initial heap or not.
    // -     Initial heap: The block count of the underlying buffer. No more is allowed.
    // - Non-initial heap: Amount of blocks that resulting heap needs to be able to allocate.
    //                     So actual allocation can be increased as needed. 
    if (!s_initialheapinitialized) {
        assert(sizeof(s_initialheapmemory) % BLOCK_SIZE == 0);
        desiredblockcount = sizetoblocks(sizeof(s_initialheapmemory), BLOCK_SIZE);
    } else {
        // Initial heap is already there, so it's time for more memory.
        desiredblockcount = sizetoblocks(minmemsize, BLOCK_SIZE);
    }

    size_t wordcount = bitmap_neededwordcount(desiredblockcount);
    size_t metadatasize = sizeof(poolheader_t) + (wordcount * sizeof(bitword_t));
    size_t totalsize = metadatasize + (desiredblockcount * BLOCK_SIZE);
    size_t pagecount = sizetoblocks(totalsize, ARCH_PAGESIZE);
    poolheader_t *pool;
    size_t bitmapsize;
    size_t poolblockcount;
    while(1) {
        if (!s_initialheapinitialized) {
            pool = (void *)s_initialheapmemory;
            // If we are using initial heap buffer, we have to make sure that total size doesn't exceed size of that buffer.
            while(sizeof(s_initialheapmemory) < (pagecount * ARCH_PAGESIZE)) {
                pagecount--;
            }
        } else {
            pool = pmalloc_alloc(&pagecount);
            if (!pool) {
                return NULL;
            }
        }
 
        // Now we calculate actual size
        size_t totalblockcount = sizetoblocks(pagecount * ARCH_PAGESIZE, BLOCK_SIZE);

        wordcount = bitmap_neededwordcount(totalblockcount);
        bitmapsize = wordcount * sizeof(bitword_t);
        // Don't forget to align!
        bitmapsize += (alignof(max_align_t) - (bitmapsize % alignof(max_align_t)));

        size_t totalsize = totalblockcount * BLOCK_SIZE;
        size_t maxpoolsize = totalsize - bitmapsize - sizeof(poolheader_t);
        poolblockcount = maxpoolsize / BLOCK_SIZE;
        // Note that for the initial heap, it is *normal* to have less pool block count than desired.
        if (!s_initialheapinitialized || (desiredblockcount < poolblockcount)) {
            break;
        }
        // It's not enough! We need more.
        pmalloc_free(pool, pagecount);
        pagecount++;
    }
    // If it is initial pool, and resulting pool block count is not enough, return NULL;
    if (!s_initialheapinitialized && (poolblockcount < sizetoblocks(minmemsize, BLOCK_SIZE))) {
        return NULL;
    }


    // Now we can initialize the pool.
    uintptr_t poolstartaddr = ((uintptr_t)pool->heapdata) + bitmapsize;
    assert((poolstartaddr % alignof(max_align_t)) == 0);
    pool->pagecount = pagecount;
    pool->blockbitmap.wordcount = wordcount;
    pool->blockbitmap.words = (bitword_t *)pool->heapdata;
    pool->blockpool = (max_align_t *)poolstartaddr;
    pool->blockcount = poolblockcount;
    pool->usedblockcount = 0;
    pool->node.data = pool;
    memset(pool->blockbitmap.words, 0, pool->blockbitmap.wordcount * sizeof(*pool->blockbitmap.words));
    bitmap_setbits(&pool->blockbitmap, 0, poolblockcount);

    tty_printf("created new pool at %p(%zuB)\n", (void *)pool, poolblockcount * BLOCK_SIZE);
    s_initialheapinitialized = true;

    struct critsect crit;
    ////////////////////////////////////////////////////////////////////////////
    critsect_begin(&crit);
    s_freeblockcount += pool->blockcount;
    list_insertback(&s_heappoollist, &pool->node, pool);

    if (CONFIG_DO_POOL_SEQUENTIAL_TEST) {
        if (!testheap(pool)) {
            panic("heap: sequential test failed");
        }
    }

    critsect_end(&crit);
    ////////////////////////////////////////////////////////////////////////////

    return pool;
}
#endif

static poolheader_t *addmem(void *mem, size_t memsize) {
    ASSERT_INTERRUPTS_DISABLED();
    size_t maxblockcount = sizetoblocks(memsize, BLOCK_SIZE);

    size_t wordcount = bitmap_neededwordcount(maxblockcount);
    size_t metadatasize = sizeof(poolheader_t) + (wordcount * sizeof(bitword_t));
    size_t maxsize = metadatasize + (maxblockcount * BLOCK_SIZE);
    size_t pagecount = sizetoblocks(maxsize, ARCH_PAGESIZE);
    poolheader_t *pool = mem;
    size_t bitmapsize;
    size_t poolblockcount;

    // We have to make sure that total size doesn't exceed size of that buffer.
    while(memsize < (pagecount * ARCH_PAGESIZE)) {
        pagecount--;
    }

    // Now we calculate the actual size
    size_t totalblockcount = sizetoblocks(pagecount * ARCH_PAGESIZE, BLOCK_SIZE);

    wordcount = bitmap_neededwordcount(totalblockcount);
    bitmapsize = wordcount * sizeof(bitword_t);
    // Don't forget to align!
    bitmapsize += (alignof(max_align_t) - (bitmapsize % alignof(max_align_t)));

    size_t totalsize = totalblockcount * BLOCK_SIZE;
    size_t maxpoolsize = totalsize - bitmapsize - sizeof(poolheader_t);
    poolblockcount = maxpoolsize / BLOCK_SIZE;

    // Initialize the pool.
    uintptr_t poolstartaddr = ((uintptr_t)pool->heapdata) + bitmapsize;
    assert((poolstartaddr % alignof(max_align_t)) == 0);
    pool->pagecount = pagecount;
    pool->blockbitmap.wordcount = wordcount;
    pool->blockbitmap.words = (bitword_t *)pool->heapdata;
    pool->blockpool = (max_align_t *)poolstartaddr;
    pool->blockcount = poolblockcount;
    pool->usedblockcount = 0;
    pool->node.data = pool;
    memset(pool->blockbitmap.words, 0, pool->blockbitmap.wordcount * sizeof(*pool->blockbitmap.words));
    bitmap_setbits(&pool->blockbitmap, 0, poolblockcount);

    s_initialheapinitialized = true;

    s_freeblockcount += pool->blockcount;
    list_insertback(&s_heappoollist, &pool->node, pool);

    if (CONFIG_DO_POOL_SEQUENTIAL_TEST) {
        if (!testheap(pool)) {
            panic("heap: sequential test failed");
        }
    }

    return pool;
}

void *heap_alloc(size_t size, uint8_t flags) {
    size_t actualsize = actualallocsize(size);
    size_t actualblockcount = sizetoblocks(actualsize, BLOCK_SIZE);

    if (size == 0) {
        return NULL;
    }
    if ((SIZE_MAX - sizeof(allocheader_t)) < size) {
        return NULL;
    }
    bool previnterrupts = arch_interrupts_disable();
    checkpoision();
    if (!s_initialheapinitialized) {
        addmem(s_initialheapmemory, sizeof(s_initialheapmemory));
    }
    void *result = NULL;
    if (actualblockcount < s_freeblockcount) {
        for (list_node_t *poolnode = s_heappoollist.front; poolnode != NULL; poolnode = poolnode->next) {
            poolheader_t *pool = poolnode->data;
            assert(pool != NULL);
            result = allocfrompool(pool, size);
            if (result != NULL) {
                break;
            }
        }
    }
    checkpoision();
    interrupts_restore(previnterrupts);
    if (flags & HEAP_FLAG_ZEROMEMORY) {
        memset(result, 0, size);
    }
    return result;
}

void heap_free(void *ptr) {
    if (ptr == NULL) {
        return;
    }
    bool previnterrupts = arch_interrupts_disable();
    allocheader_t *alloc = allocheaderof(ptr);
    checkpoision();
    list_removenode(&s_alloclist, &alloc->node);
    if (alloc == NULL) {
        goto die;
    }
    if (!alloc->pool) {
        goto die;
    }
    uintptr_t poolstartaddr = (uintptr_t)alloc->pool->blockpool;
    uintptr_t poolendaddr = poolstartaddr + (alloc->pool->blockcount * BLOCK_SIZE);
    uintptr_t allocstartaddr = (uintptr_t)ptr;
    uintptr_t allocendaddr = (uintptr_t)alloc + alloc->blockcount * BLOCK_SIZE;
    if ((allocstartaddr < poolstartaddr) || (poolendaddr <= allocstartaddr)) {
        goto die;
    }
    if ((allocendaddr <= poolstartaddr) || (poolendaddr < allocendaddr)) {
        goto die;
    }
    if (alloc->pool->usedblockcount < alloc->blockcount) {
        goto die;
    }
    uintptr_t offsetinpool = (uintptr_t)alloc - poolstartaddr;
    bitindex_t blockindex = offsetinpool / BLOCK_SIZE;
    bitmap_setbits(&alloc->pool->blockbitmap, blockindex, alloc->blockcount);
    alloc->pool->usedblockcount -= alloc->blockcount;
    s_freeblockcount += alloc->blockcount;
    memset(alloc, 0x6f, alloc->blockcount * BLOCK_SIZE);
    checkpoision();
    interrupts_restore(previnterrupts);
    return;
die:
    panic("heap_free: bad pointer");
}

void *heap_realloc(void *ptr, size_t newsize, uint8_t flags) {
    if (ptr == NULL) {
        return heap_alloc(newsize, flags);
    }
    bool previnterrupts = arch_interrupts_disable();
    allocheader_t *alloc = allocheaderof(ptr);
    checkpoision();
    if (alloc == NULL) {
        goto die;
    }
    size_t copysize;
    if (newsize < alloc->size) {
        copysize = newsize;
    } else {
        copysize = alloc->size;
    }
    void *newmem = heap_alloc(newsize, flags);
    if (newmem == NULL) {
        goto out;
    }
    memcpy(newmem, ptr, copysize);
    heap_free(ptr);
out:
    interrupts_restore(previnterrupts);
    return newmem;
die:
    panic("heap_realloc: bad pointer");
}

void *heap_calloc(size_t size, size_t elements, uint8_t flags) {
    if ((SIZE_MAX / size) < elements) {
        return NULL;
    }
    return heap_alloc(size * elements, flags);
}

void *heap_reallocarray(void *ptr, size_t newsize, size_t newelements, uint8_t flags) {
    if ((SIZE_MAX / newsize) < newelements) {
        return NULL;
    }
    return heap_realloc(ptr, newsize * newelements, flags);
}

static size_t const MAXEXPANDSIZE = 16 * 1024 * 1024;

void heap_expand(void) {
    bool previnterrupts = arch_interrupts_disable();
    vmobject_t *object;
    size_t heapsize = pmm_get_totalmem();
    if (MAXEXPANDSIZE < heapsize) {
        heapsize = MAXEXPANDSIZE;
    }
    status_t status = vmm_alloc(&object, vmm_get_kernel_addressspace(), heapsize, MAP_PROT_READ | MAP_PROT_WRITE);
    if (status != OK) {
        tty_printf("could not expand heap (error %d)\n", status);
        goto out;
    }
    addmem((void *)object->startaddress, object->endaddress - object->startaddress + 1);
out:
    interrupts_restore(previnterrupts);
}

////////////////////////////////////////////////////////////////////////////////

enum {
    RAND_TEST_ALLOC_COUNT = 10,
    MAX_ALLOC_SIZE        = (1024 * 1024 * 2) / RAND_TEST_ALLOC_COUNT,
};

bool heap_run_random_test(void) {
    size_t allocsizes[RAND_TEST_ALLOC_COUNT];
    void **allocptrs[RAND_TEST_ALLOC_COUNT];

    for (size_t i = 0; i < RAND_TEST_ALLOC_COUNT; i++) {
        while(1) {
            allocsizes[i] = (size_t)rand() % MAX_ALLOC_SIZE;
            allocptrs[i] = heap_alloc(allocsizes[i], 0);
            if (allocptrs[i] != 0) {
                break;
            }
        }
    }
    for (size_t i = 0; i < RAND_TEST_ALLOC_COUNT; i++) {
        size_t ptrcount = allocsizes[i] / sizeof(void *); 
        for (size_t j = 0; j < ptrcount; j++) {
            allocptrs[i][j] = &allocptrs[i][j];
        }
    }
    for (size_t i = 0; i < RAND_TEST_ALLOC_COUNT; i++) {
        size_t ptrcount = allocsizes[i] / sizeof(void *); 
        for (size_t j = 0; j < ptrcount; j++) {
            void *expectedvalue = &allocptrs[i][j];
            void *gotvalue = allocptrs[i][j];
            if (allocptrs[i][j] != gotvalue) {
                tty_printf("value mismatch at %p(allocation %zu, base %p, offset %zu): expected %p, got %p\n", &allocptrs[i][j], i, &allocptrs[i], j, expectedvalue, gotvalue);
                goto testfail;
            }
        }
    }
    for (size_t i = 0; i < RAND_TEST_ALLOC_COUNT; i++) {
        heap_free(allocptrs[i]);
    }
    return true;
testfail:
    return false;
}

