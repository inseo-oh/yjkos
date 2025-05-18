#include <kernel/arch/interrupts.h>
#include <kernel/arch/mmu.h>
#include <kernel/io/co.h>
#include <kernel/lib/bitmap.h>
#include <kernel/lib/diagnostics.h>
#include <kernel/lib/list.h>
#include <kernel/lib/miscmath.h>
#include <kernel/mem/heap.h>
#include <kernel/mem/pmm.h>
#include <kernel/mem/vmm.h>
#include <kernel/panic.h>
#include <kernel/types.h>
#include <stdalign.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/******************************** Configuration *******************************/

/*
 * Should we run sequential allocation test when a heap pool initializes?
 * This can take a *very* long time depending on CPU speed and how large the
 * pool is.
 */
static bool const CONFIG_DO_POOL_SEQUENTIAL_TEST = false;
/*
 * Should sequential test be verbose?
 */
static bool const CONFIG_SEQUENTIAL_TEST_VERBOSE = true;

/******************************************************************************/

static uint8_t const POISONVALUES[] = {
    0xe9, 0x29, 0xf3, 0xfb,
    0xd7, 0x67, 0xaa, 0x5a};

struct pool_header {
    max_align_t *blockpool;
    struct List_Node node;
    struct Bitmap blockbitmap;
    size_t block_count;
    size_t usedblock_count;
    size_t page_count;
    max_align_t heapdata[];
};

struct alloc_header {
    struct List_Node node;
    struct pool_header *pool;
    size_t block_count, size;
    max_align_t data[];
};

#define BLOCK_SIZE 64

static size_t s_free_block_count = 0;
static struct List s_heap_pool_list; /* pool_header items */
static struct List s_alloc_list;     /* alloc_header items */
static bool s_initial_heap_initialized = false;

static uint8_t s_initial_heap_memory[1024 * 1024 * 2];

STATIC_ASSERT_TEST(alignof(struct pool_header) == alignof(max_align_t));

static size_t byte_count_for_block_count(size_t block_count) {
    return (BLOCK_SIZE * block_count) -
           (sizeof(struct alloc_header) + sizeof(POISONVALUES));
}

static size_t actual_alloc_size(size_t size) {
    return size + sizeof(struct alloc_header) + sizeof(POISONVALUES);
}

void __Heap_CheckOverflow(struct SourceLocation srcloc) {
    bool prev_interrupts = Arch_Irq_Disable();
    bool die = false;
    LIST_FOREACH(&s_alloc_list, allocnode) {
        bool corrupted = false;
        struct alloc_header *alloc = allocnode->data;
        if (alloc == NULL) {
            Co_Printf("heap: list node pointer is null\n");
            corrupted = true;
            goto checkend;
        }
        PHYSPTR physaddr = 0;
        int ret = Arch_Mmu_VirtToPhys(&physaddr, alloc);
        if (ret < 0) {
            Co_Printf("heap: bad alloc ptr(error %d)\n", ret);
            corrupted = true;
            goto checkend;
        }
        uint8_t *poision = &((uint8_t *)alloc->data)[alloc->size];
        for (size_t i = 0; i < sizeof(POISONVALUES); i++) {
            if (poision[i] != POISONVALUES[i]) {
                Co_Printf("heap: bad poision value at offset %zu: expected %02x, got %02x\n", i, POISONVALUES[i], poision[i]);
                corrupted = true;
            }
        }
    checkend:
        if (corrupted) {
            Co_Printf("heap: allocation at %p(node: %p) is corrupted\n", alloc, allocnode);
            Co_Printf("heap: checked at %s:%d <%s>\n", srcloc.filename, srcloc.line, srcloc.function);
            die = true;
        }
    }
    if (die) {
        Panic("heap overflow detected");
    }
    Arch_Irq_Restore(prev_interrupts);
}

static void *alloc_from_pool(struct pool_header *self, size_t size) {
    ASSERT_IRQ_DISABLED();
    if (size == 0) {
        return NULL;
    }
    struct alloc_header *alloc = NULL;
    if ((SIZE_MAX - sizeof(struct alloc_header)) < size) {
        return NULL;
    }
    size_t actual_size = actual_alloc_size(size);
    size_t block_count = SizeToBlocks(actual_size, BLOCK_SIZE);
    long block_index = Bitmap_FindSetBits(&self->blockbitmap, 0, block_count);
    if (block_index < 0) {
        goto out;
    }
    for (size_t i = 0; i < block_count; i++) {
        assert(Bitmap_IsBitSet(&self->blockbitmap, block_index + i));
    }
    Bitmap_ClearBits(&self->blockbitmap, block_index, block_count);
    for (size_t i = 0; i < block_count; i++) {
        assert(!Bitmap_IsBitSet(&self->blockbitmap, block_index + i));
    }
    uintptr_t alloc_off = block_index * BLOCK_SIZE;
    assert(IsAligned(alloc_off, alignof(max_align_t)));
    alloc = (struct alloc_header *)((char *)self->blockpool + alloc_off);
    self->usedblock_count += block_count;
out:
    if (alloc == NULL) {
        return NULL;
    }
    alloc->pool = self;
    alloc->block_count = block_count;
    alloc->size = size;
    assert(block_count <= s_free_block_count);
    s_free_block_count -= block_count;
    memset(alloc->data, 0x90, size);
    /* Setup poision values ***************************************************/
    uint8_t *poisiondest = &((uint8_t *)alloc->data)[size];
    for (size_t i = 0; i < sizeof(POISONVALUES); i++) {
        poisiondest[i] = POISONVALUES[i];
    }
    List_InsertBack(&s_alloc_list, &alloc->node, alloc);
    HEAP_CHECKOVERFLOW();
    return alloc->data;
}

static struct alloc_header *alloc_header_of(void *ptr) {
    if ((ptr == NULL) || (!IsAligned((uintptr_t)ptr, alignof(max_align_t))) || ((uintptr_t)ptr < offsetof(struct alloc_header, data))) {
        return NULL;
    }
    return (struct alloc_header *)(void *)((char *)ptr - offsetof(struct alloc_header, data));
}

[[nodiscard]] static char *test_Heap_Alloc(struct pool_header *self, size_t alloc_block_count, int type, char *expected_ptr) {
    uintptr_t pool_start_addr = (uintptr_t)self->blockpool;
    uintptr_t pool_end_addr = pool_start_addr + ((BLOCK_SIZE * self->block_count) - 1);
    size_t byte_count = byte_count_for_block_count(alloc_block_count);
    char *alloc = NULL;
    if (type == 0) {
        alloc = alloc_from_pool(self, byte_count);
        if (expected_ptr != alloc) {
            Arch_Irq_Disable();
            Co_Printf("unexpected address\n");
            goto failed;
        }
    } else {
        alloc = expected_ptr;
    }
    if (!IsAligned((uintptr_t)alloc, alignof(max_align_t))) {
        Arch_Irq_Disable();
        Co_Printf("misaligned allocation\n");
        goto failed;
    }

    if (pool_end_addr < (uintptr_t)alloc) {
        Arch_Irq_Disable();
        Co_Printf("address beyond end of the heap\n");
        goto failed;
    }
    struct alloc_header *alloc_header = alloc_header_of(alloc);
    if (alloc_header->pool != self) {
        Arch_Irq_Disable();
        Co_Printf("bad pool pointer\n");
        goto failed_with_alloc_header;
    }
    if (alloc_header->block_count != alloc_block_count) {
        Arch_Irq_Disable();
        Co_Printf("incorrect block count\n");
        goto failed_with_alloc_header;
    }
    if (alloc_header->data != (void *)alloc) {
        Arch_Irq_Disable();
        Co_Printf("incorrect data start\n");
        goto failed_with_alloc_header;
    }
    /* If it's not type 0, we already overwrote memory with other values, so don't check for initial pattern. */
    if (type == 0 && (*((uint32_t *)alloc) != 0x90909090)) {
        Arch_Irq_Disable();
        Co_Printf("incorrect initial pattern (got %p)\n", *((uint32_t *)alloc));
        goto failed_with_alloc_header;
    }
    return alloc;
failed_with_alloc_header:
    Co_Printf(" - alloc header:\n");
    Co_Printf(" +-- region pointer: %p\n", alloc_header->pool);
    Co_Printf(" +-- block count:    %u\n", alloc_header->block_count);
    Co_Printf(" +-- data start:     %p\n", alloc_header->data);
failed:
    Heap_Free(alloc);
    return NULL;
}

[[nodiscard]] static bool test_Heap_Alloc_and_fill(struct pool_header *self, size_t alloc_count, size_t alloc_block_count) {
    size_t byte_count = byte_count_for_block_count(alloc_block_count);

    /*
     * We run the basic allocation checks twice, to see if filling the returned
     * memory overwrote crucial data structures.
     * - type=1: First iteration  - Allocate and check, and fill
     * - type=2: Second iteration - Check only the allocation header
     */
    for (int type = 0; type < 2; type++) {
        char *expected_bptr = (char *)self->blockpool + sizeof(struct alloc_header);
        if (CONFIG_SEQUENTIAL_TEST_VERBOSE) {
            if (type == 0) {
                Co_Printf("[allocate&fill]");
            } else {
                Co_Printf(" [basic re-check]");
            }
        }
        for (size_t i = 0; i < alloc_count; i++) {
            char *alloc = test_Heap_Alloc(self, alloc_block_count, type, expected_bptr);
            if (alloc == NULL) {
                goto failed;
            }
            if (type == 0) {
                for (size_t j = 0; j < byte_count; j++) {
                    alloc[j] = (char)(i & 0xffU);
                }
            }
            expected_bptr += BLOCK_SIZE * alloc_block_count;
            continue;
        failed:
            Co_Printf("- alloc pointer:     %p\n", alloc);
            Co_Printf("- expected pointer:  %#p\n", expected_bptr);
            Co_Printf("- alloc index:       %u/%u\n", i, (alloc_count - 1));
            return false;
        }
    }
    return true;
}

[[nodiscard]] static bool test_heap_compare(struct pool_header *self, size_t alloc_count, size_t alloc_block_count) {
    size_t byte_count = byte_count_for_block_count(alloc_block_count);
    if (CONFIG_SEQUENTIAL_TEST_VERBOSE) {
        Co_Printf(" [compare]");
    }
    uint8_t *bptr = (UCHAR *)self->blockpool + sizeof(struct alloc_header);
    for (size_t i = 0; i < alloc_count; i++) {
        uint8_t *alloc = bptr;
        for (size_t j = 0; j < byte_count; j++) {
            if (alloc[j] != (i & 0xffU)) {
                Co_Printf("corrupted data\n");
                Co_Printf("- allocated at:      %p\n", bptr);
                Co_Printf("- alloc index:       %u/%u\n", i, (alloc_count - 1));
                Co_Printf("- byte offset:       %u/%u\n", j, byte_count - 1);
                Co_Printf("- expected:          %u\n", i);
                Co_Printf("- got:               %u\n", alloc[j]);
                return false;
            }
        }
        bptr += BLOCK_SIZE * alloc_block_count;
    }
    return true;
}

[[nodiscard]] static bool test_Heap_Free(struct pool_header *self, size_t alloc_count, size_t alloc_block_count) {
    size_t byte_count = byte_count_for_block_count(alloc_block_count);
    if (CONFIG_SEQUENTIAL_TEST_VERBOSE) {
        Co_Printf(" [free]");
    }
    uint8_t *bptr = (UCHAR *)self->blockpool + sizeof(struct alloc_header);
    for (size_t i = 0; i < alloc_count; i++) {
        UCHAR *alloc = bptr;
        Heap_Free(alloc);
        for (size_t j = 0; j < byte_count; j++) {
            if (alloc[j] != 0x6f) {
                Co_Printf("free pattern(0x6f) not found\n");
                Co_Printf("- allocated at:      %#lx\n", bptr);
                Co_Printf("- alloc index:       %u/%u\n", i, (alloc_count - 1));
                Co_Printf("- byte offset:       %u/%u\n", j, byte_count - 1);
                Co_Printf("- got:               %u\n", alloc[j]);
                return false;
            }
        }
        bptr += BLOCK_SIZE * alloc_block_count;
    }
    return true;
}

static bool test_heap(struct pool_header *self) {
    size_t alloc_block_count = 1;
    uintptr_t pool_start_addr = (uintptr_t)self->blockpool;
    uintptr_t pool_end_addr = pool_start_addr + ((BLOCK_SIZE * self->block_count) - 1);
    Co_Printf("sequential memory test start(%#lx~%#lx, size %zu)\n", pool_start_addr, pool_end_addr, pool_end_addr - pool_start_addr + 1);

    while (1) {
        Co_Printf("- alloc byte count:  %u\n", byte_count_for_block_count(alloc_block_count));

        size_t alloc_count = self->block_count / alloc_block_count;
        if (alloc_count == 0) {
            break;
        }
        if (CONFIG_SEQUENTIAL_TEST_VERBOSE) {
            Co_Printf("* Testing %u x %u blocks: ", alloc_count, alloc_block_count);
        }
        if (!test_Heap_Alloc_and_fill(self, alloc_count, alloc_block_count)) {
            goto testfailed;
        }
        if (!test_heap_compare(self, alloc_count, alloc_block_count)) {
            goto testfailed;
        }
        if (!test_Heap_Free(self, alloc_count, alloc_block_count)) {
            goto testfailed;
        }
        alloc_block_count++;
        Co_Printf(" -> OK!\n");
        continue;
    testfailed:
        Co_Printf("- alloc block count: %u\n", alloc_block_count);
        Co_Printf("- pool start addr:   %#lx\n", pool_start_addr);
        Co_Printf("- pool end addr:     %#lx\n", pool_end_addr);
        return false;
    }
    Co_Printf("sequential memory test ok(%#lx~%#lx)\n", pool_start_addr, pool_end_addr - 1);
    return true;
}

/*
 * Backup of old code for implementing userspace malloc in the future. It will no longer work as-is though.
 * (It allocates new heap based on desired size)
 */
#if 0
static pool_header_t *createpool(size_t minmemsize) {
    size_t desiredblock_count;
    /*
     * Note that desiredblock_count works a bit different depending on whether it's initial heap or not.
     * -     Initial heap: The block count of the underlying buffer. No more is allowed.
     * - Non-initial heap: Amount of blocks that resulting heap needs to be able to allocate.
     *                     So actual allocation can be increased as needed. 
     */
    if (!s_initialheapinitialized) {
        assert(sizeof(s_initialheapmemory) % BLOCK_SIZE == 0);
        desiredblock_count = size_to_blocks(sizeof(s_initialheapmemory), BLOCK_SIZE);
    } else {
        /* Initial heap is already there, so it's time for more memory. */
        desiredblock_count = size_to_blocks(minmemsize, BLOCK_SIZE);
    }

    size_t word_count = bitmap_needed_word_count(desiredblock_count);
    size_t metadatasize = sizeof(pool_header_t) + (word_count * sizeof(UINT));
    size_t totalsize = metadatasize + (desiredblock_count * BLOCK_SIZE);
    size_t page_count = size_to_blocks(totalsize, ARCH_PAGESIZE);
    pool_header_t *pool;
    size_t bitmapsize;
    size_t poolblock_count;
    while(1) {
        if (!s_initialheapinitialized) {
            pool = (void *)s_initialheapmemory;
            /* If we are using initial heap buffer, we have to make sure that total size doesn't exceed size of that buffer. */
            while(sizeof(s_initialheapmemory) < (page_count * ARCH_PAGESIZE)) {
                page_count--;
            }
        } else {
            pool = pmalloc_alloc(&page_count);
            if (!pool) {
                return NULL;
            }
        }
 
        /* Now we calculate actual size */
        size_t totalblock_count = size_to_blocks(page_count * ARCH_PAGESIZE, BLOCK_SIZE);

        word_count = bitmap_needed_word_count(totalblock_count);
        bitmapsize = word_count * sizeof(UINT);
        /* Don't forget to align! */
        bitmapsize += (alignof(max_align_t) - (bitmapsize % alignof(max_align_t)));

        size_t totalsize = totalblock_count * BLOCK_SIZE;
        size_t maxpoolsize = totalsize - bitmapsize - sizeof(pool_header_t);
        poolblock_count = maxpoolsize / BLOCK_SIZE;
        /* Note that for the initial heap, it is *normal* to have less pool block count than desired. */
        if (!s_initialheapinitialized || (desiredblock_count < poolblock_count)) {
            break;
        }
        /* It's not enough! We need more. */
        pmalloc_free(pool, page_count);
        page_count++;
    }
    /* If it is initial pool, and resulting pool block count is not enough, return NULL; */
    if (!s_initialheapinitialized && (poolblock_count < size_to_blocks(minmemsize, BLOCK_SIZE))) {
        return NULL;
    }


    /* Now we can initialize the pool. */
    uintptr_t poolstartaddr = ((uintptr_t)pool->heapdata) + bitmapsize;
    assert((poolstartaddr % alignof(max_align_t)) == 0);
    pool->page_count = page_count;
    pool->blockbitmap.word_count = word_count;
    pool->blockbitmap.words = (UINT *)pool->heapdata;
    pool->blockpool = (max_align_t *)poolstartaddr;
    pool->block_count = poolblock_count;
    pool->usedblock_count = 0;
    pool->node.data = pool;
    memset(pool->blockbitmap.words, 0, pool->blockbitmap.word_count * sizeof(*pool->blockbitmap.words));
    bitmap_setbits(&pool->blockbitmap, 0, poolblock_count);

    tty_printf("created new pool at %p(%zuB)\n", (void *)pool, poolblock_count * BLOCK_SIZE);
    s_initialheapinitialized = true;

    struct critsect crit;
    /**************************************************************************/
    critsect_begin(&crit);
    s_freeblock_count += pool->block_count;
    List_InsertBack(&s_heappoollist, &pool->node, pool);

    if (CONFIG_DO_POOL_SEQUENTIAL_TEST) {
        if (!testheap(pool)) {
            panic("heap: sequential test failed");
        }
    }

    critsect_end(&crit);
    /**************************************************************************/

    return pool;
}
#endif

static struct pool_header *add_mem(void *mem, size_t memsize) {
    ASSERT_IRQ_DISABLED();
    size_t max_block_count = SizeToBlocks(memsize, BLOCK_SIZE);

    size_t word_count = Bitmap_NeededWordCount(max_block_count);
    size_t metadata_size = sizeof(struct pool_header) + (word_count * sizeof(UINT));
    size_t maxsize = metadata_size + (max_block_count * BLOCK_SIZE);
    size_t page_count = SizeToBlocks(maxsize, ARCH_PAGESIZE);
    struct pool_header *pool = mem;
    size_t bitmapsize = 0;
    size_t poolblock_count = 0;

    /* We have to make sure that total size doesn't exceed size of that buffer. */
    while (memsize < (page_count * ARCH_PAGESIZE)) {
        page_count--;
    }

    /* Now we calculate the actual size ***************************************/
    size_t totalblock_count = SizeToBlocks(page_count * ARCH_PAGESIZE, BLOCK_SIZE);

    word_count = Bitmap_NeededWordCount(totalblock_count);
    bitmapsize = word_count * sizeof(UINT);
    /* Don't forget to align! *************************************************/
    bitmapsize += (alignof(max_align_t) - (bitmapsize % alignof(max_align_t)));

    size_t totalsize = totalblock_count * BLOCK_SIZE;
    size_t maxpoolsize = totalsize - bitmapsize - sizeof(struct pool_header);
    poolblock_count = maxpoolsize / BLOCK_SIZE;

    /* Initialize the pool.****************************************************/
    char *poolstart = ((char *)pool->heapdata) + bitmapsize;
    assert(((uintptr_t)poolstart % alignof(max_align_t)) == 0);
    pool->page_count = page_count;
    pool->blockbitmap.word_count = word_count;
    pool->blockbitmap.words = (UINT *)pool->heapdata;
    pool->blockpool = (max_align_t *)poolstart;
    pool->block_count = poolblock_count;
    pool->usedblock_count = 0;
    pool->node.data = pool;
    memset(pool->blockbitmap.words, 0, pool->blockbitmap.word_count * sizeof(*pool->blockbitmap.words));
    Bitmap_SetBits(&pool->blockbitmap, 0, poolblock_count);

    s_initial_heap_initialized = true;

    s_free_block_count += pool->block_count;
    List_InsertBack(&s_heap_pool_list, &pool->node, pool);

    if (CONFIG_DO_POOL_SEQUENTIAL_TEST) {
        if (!test_heap(pool)) {
            Panic("heap: sequential test failed");
        }
    }

    return pool;
}

void *Heap_Alloc(size_t size, uint8_t flags) {
    size_t actualsize = actual_alloc_size(size);
    size_t actualblock_count = SizeToBlocks(actualsize, BLOCK_SIZE);

    if (size == 0) {
        return NULL;
    }
    if ((SIZE_MAX - sizeof(struct alloc_header)) < size) {
        return NULL;
    }
    bool prev_interrupts = Arch_Irq_Disable();
    HEAP_CHECKOVERFLOW();
    if (!s_initial_heap_initialized) {
        add_mem(s_initial_heap_memory, sizeof(s_initial_heap_memory));
    }
    void *result = NULL;
    if (actualblock_count < s_free_block_count) {
        LIST_FOREACH(&s_heap_pool_list, poolnode) {
            struct pool_header *pool = poolnode->data;
            assert(pool != NULL);
            result = alloc_from_pool(pool, size);
            if (result != NULL) {
                break;
            }
        }
    }
    HEAP_CHECKOVERFLOW();
    Arch_Irq_Restore(prev_interrupts);
    if (flags & HEAP_FLAG_ZEROMEMORY) {
        memset(result, 0, size);
    }
    return result;
}

void Heap_Free(void *ptr) {
    if (ptr == NULL) {
        return;
    }
    bool prev_interrupts = Arch_Irq_Disable();
    struct alloc_header *alloc = alloc_header_of(ptr);
    HEAP_CHECKOVERFLOW();
    List_RemoveNode(&s_alloc_list, &alloc->node);
    if (alloc == NULL) {
        goto die;
    }
    if (!alloc->pool) {
        goto die;
    }
    uintptr_t poolstartaddr = (uintptr_t)alloc->pool->blockpool;
    uintptr_t poolendaddr = poolstartaddr + (alloc->pool->block_count * BLOCK_SIZE);
    uintptr_t allocstartaddr = (uintptr_t)ptr;
    uintptr_t allocendaddr = (uintptr_t)alloc + alloc->block_count * BLOCK_SIZE;
    if ((allocstartaddr < poolstartaddr) || (poolendaddr <= allocstartaddr)) {
        goto die;
    }
    if ((allocendaddr <= poolstartaddr) || (poolendaddr < allocendaddr)) {
        goto die;
    }
    if (alloc->pool->usedblock_count < alloc->block_count) {
        goto die;
    }
    uintptr_t offsetinpool = (uintptr_t)alloc - poolstartaddr;
    size_t blockindex = offsetinpool / BLOCK_SIZE;
    Bitmap_SetBits(&alloc->pool->blockbitmap, (long)blockindex, alloc->block_count);
    alloc->pool->usedblock_count -= alloc->block_count;
    s_free_block_count += alloc->block_count;
    memset(alloc, 0x6f, alloc->block_count * BLOCK_SIZE);
    HEAP_CHECKOVERFLOW();
    Arch_Irq_Restore(prev_interrupts);
    return;
die:
    Panic("Heap_Free: bad pointer");
}

void *Heap_Realloc(void *ptr, size_t newsize, uint8_t flags) {
    if (ptr == NULL) {
        return Heap_Alloc(newsize, flags);
    }
    bool prev_interrupts = Arch_Irq_Disable();
    struct alloc_header *alloc = alloc_header_of(ptr);
    HEAP_CHECKOVERFLOW();
    if (alloc == NULL) {
        goto die;
    }
    size_t copysize = 0;
    if (newsize < alloc->size) {
        copysize = newsize;
    } else {
        copysize = alloc->size;
    }
    void *newmem = Heap_Alloc(newsize, flags);
    if (newmem == NULL) {
        goto out;
    }
    memcpy(newmem, ptr, copysize);
    Heap_Free(ptr);
out:
    Arch_Irq_Restore(prev_interrupts);
    return newmem;
die:
    Panic("Heap_Realloc: bad pointer");
}

void *Heap_Calloc(size_t size, size_t elements, uint8_t flags) {
    if ((SIZE_MAX / size) < elements) {
        return NULL;
    }
    return Heap_Alloc(size * elements, flags);
}

void *Heap_ReallocArray(void *ptr, size_t newsize, size_t newelements, uint8_t flags) {
    if ((SIZE_MAX / newsize) < newelements) {
        return NULL;
    }
    return Heap_Realloc(ptr, newsize * newelements, flags);
}

static size_t const MAXEXPANDSIZE = 16 * 1024 * 1024;

void Heap_Expand(void) {
    bool prev_interrupts = Arch_Irq_Disable();
    struct Vmm_Object *object = NULL;
    size_t heapsize = Pmm_GetTotalMem();
    if (MAXEXPANDSIZE < heapsize) {
        heapsize = MAXEXPANDSIZE;
    }
    object = Vmm_Alloc(Vmm_GetKernelAddressSpace(), heapsize, MAP_PROT_READ | MAP_PROT_WRITE);
    if (object == NULL) {
        Co_Printf("not enough memory to expand heap\n");
        goto out;
    }
    add_mem(object->start, Vmm_GetObjectSize((object)));
out:
    Arch_Irq_Restore(prev_interrupts);
}

/******************************************************************************/

#define RAND_TEST_ALLOC_COUNT 10
#define MAX_ALLOC_SIZE ((1024 * 1024 * 2) / RAND_TEST_ALLOC_COUNT)

bool Heap_RunRandomTest(void) {
    size_t allocsizes[RAND_TEST_ALLOC_COUNT];
    void **allocptrs[RAND_TEST_ALLOC_COUNT];

    for (size_t i = 0; i < RAND_TEST_ALLOC_COUNT; i++) {
        while (1) {
            allocsizes[i] = (size_t)rand() % MAX_ALLOC_SIZE;
            allocptrs[i] = Heap_Alloc(allocsizes[i], 0);
            if (allocptrs[i] != 0) {
                break;
            }
        }
    }
    for (size_t i = 0; i < RAND_TEST_ALLOC_COUNT; i++) {
        size_t ptr_count = allocsizes[i] / sizeof(void *);
        for (size_t j = 0; j < ptr_count; j++) {
            allocptrs[i][j] = (void *)&allocptrs[i][j];
        }
    }
    for (size_t i = 0; i < RAND_TEST_ALLOC_COUNT; i++) {
        size_t ptr_count = allocsizes[i] / sizeof(void *);
        for (size_t j = 0; j < ptr_count; j++) {
            void *expectedvalue = &allocptrs[i][j];
            void *gotvalue = allocptrs[i][j];
            if (allocptrs[i][j] != gotvalue) {
                Co_Printf("value mismatch at %p(allocation %zu, base %p, offset %zu): expected %p, got %p\n", &allocptrs[i][j], i, &allocptrs[i], j, expectedvalue, gotvalue);
                goto testfail;
            }
        }
    }
    for (size_t i = 0; i < RAND_TEST_ALLOC_COUNT; i++) {
        Heap_Free((void *)allocptrs[i]);
    }
    return true;
testfail:
    return false;
}
