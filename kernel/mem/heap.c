#include <kernel/arch/interrupts.h>
#include <kernel/arch/mmu.h>
#include <kernel/io/co.h>
#include <kernel/lib/bitmap.h>
#include <kernel/lib/diagnostics.h>
#include <kernel/lib/list.h>
#include <kernel/lib/miscmath.h>
#include <kernel/lib/strutil.h>
#include <kernel/mem/heap.h>
#include <kernel/mem/pmm.h>
#include <kernel/mem/vmm.h>
#include <kernel/panic.h>
#include <kernel/types.h>
#include <stdalign.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

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
    struct list_node node;
    struct bitmap blockbitmap;
    size_t block_count;
    size_t usedblock_count;
    size_t page_count;
    max_align_t heapdata[];
};

struct alloc_header {
    struct list_node node;
    struct pool_header *pool;
    size_t block_count, size;
    max_align_t data[];
};

#define BLOCK_SIZE 64

static size_t s_free_block_count = 0;
static struct list s_heap_pool_list; /* pool_header items */
static struct list s_alloc_list;     /* alloc_header items */
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

void __heap_check_overflow(struct source_location srcloc) {
    bool prev_interrupts = arch_irq_disable();
    bool die = false;
    LIST_FOREACH(&s_alloc_list, allocnode) {
        bool corrupted = false;
        struct alloc_header *alloc = allocnode->data;
        if (alloc == nullptr) {
            co_printf("heap: list node pointer is null\n");
            corrupted = true;
            goto checkend;
        }
        PHYSPTR physaddr = 0;
        int ret = arch_mmu_virtual_to_physical(&physaddr, alloc);
        if (ret < 0) {
            co_printf("heap: bad alloc ptr(error %d)\n", ret);
            corrupted = true;
            goto checkend;
        }
        uint8_t *poision = &((uint8_t *)alloc->data)[alloc->size];
        for (size_t i = 0; i < sizeof(POISONVALUES); i++) {
            if (poision[i] != POISONVALUES[i]) {
                co_printf("heap: bad poision value at offset %zu: expected %02x, got %02x\n", i, POISONVALUES[i], poision[i]);
                corrupted = true;
            }
        }
    checkend:
        if (corrupted) {
            co_printf("heap: allocation at %p(node: %p) is corrupted\n", alloc, allocnode);
            co_printf("heap: checked at %s:%d <%s>\n", srcloc.filename, srcloc.line, srcloc.function);
            die = true;
        }
    }
    if (die) {
        panic("heap overflow detected");
    }
    arch_irq_restore(prev_interrupts);
}

static void *alloc_from_pool(struct pool_header *self, size_t size) {
    ASSERT_IRQ_DISABLED();
    if (size == 0) {
        return nullptr;
    }
    struct alloc_header *alloc = nullptr;
    if ((SIZE_MAX - sizeof(struct alloc_header)) < size) {
        return nullptr;
    }
    size_t actual_size = actual_alloc_size(size);
    size_t block_count = size_to_blocks(actual_size, BLOCK_SIZE);
    long block_index = bitmap_find_set_bits(&self->blockbitmap, 0, block_count);
    if (block_index < 0) {
        goto out;
    }
    for (size_t i = 0; i < block_count; i++) {
        assert(bitmap_is_bit_set(&self->blockbitmap, block_index + i));
    }
    bitmap_clear_bits(&self->blockbitmap, block_index, block_count);
    for (size_t i = 0; i < block_count; i++) {
        assert(!bitmap_is_bit_set(&self->blockbitmap, block_index + i));
    }
    uintptr_t alloc_off = block_index * BLOCK_SIZE;
    assert(is_aligned(alloc_off, alignof(max_align_t)));
    alloc = (struct alloc_header *)((char *)self->blockpool + alloc_off);
    self->usedblock_count += block_count;
out:
    if (alloc == nullptr) {
        return nullptr;
    }
    alloc->pool = self;
    alloc->block_count = block_count;
    alloc->size = size;
    assert(block_count <= s_free_block_count);
    s_free_block_count -= block_count;
    vmemset(alloc->data, 0x90, size);
    /* Setup poision values ***************************************************/
    uint8_t *poisiondest = &((uint8_t *)alloc->data)[size];
    for (size_t i = 0; i < sizeof(POISONVALUES); i++) {
        poisiondest[i] = POISONVALUES[i];
    }
    list_insert_back(&s_alloc_list, &alloc->node, alloc);
    HEAP_CHECKOVERFLOW();
    return alloc->data;
}

static struct alloc_header *alloc_header_of(void *ptr) {
    if ((ptr == nullptr) || (!is_aligned((uintptr_t)ptr, alignof(max_align_t))) || ((uintptr_t)ptr < offsetof(struct alloc_header, data))) {
        return nullptr;
    }
    return (struct alloc_header *)(void *)((char *)ptr - offsetof(struct alloc_header, data));
}

[[nodiscard]] static char *test_heap_alloc(struct pool_header *self, size_t alloc_block_count, int type, char *expected_ptr) {
    uintptr_t pool_start_addr = (uintptr_t)self->blockpool;
    uintptr_t pool_end_addr = pool_start_addr + ((BLOCK_SIZE * self->block_count) - 1);
    size_t byte_count = byte_count_for_block_count(alloc_block_count);
    char *alloc = nullptr;
    if (type == 0) {
        alloc = alloc_from_pool(self, byte_count);
        if (expected_ptr != alloc) {
            arch_irq_disable();
            co_printf("unexpected address\n");
            goto failed;
        }
    } else {
        alloc = expected_ptr;
    }
    if (!is_aligned((uintptr_t)alloc, alignof(max_align_t))) {
        arch_irq_disable();
        co_printf("misaligned allocation\n");
        goto failed;
    }

    if (pool_end_addr < (uintptr_t)alloc) {
        arch_irq_disable();
        co_printf("address beyond end of the heap\n");
        goto failed;
    }
    struct alloc_header *alloc_header = alloc_header_of(alloc);
    if (alloc_header->pool != self) {
        arch_irq_disable();
        co_printf("bad pool pointer\n");
        goto failed_with_alloc_header;
    }
    if (alloc_header->block_count != alloc_block_count) {
        arch_irq_disable();
        co_printf("incorrect block count\n");
        goto failed_with_alloc_header;
    }
    if (alloc_header->data != (void *)alloc) {
        arch_irq_disable();
        co_printf("incorrect data start\n");
        goto failed_with_alloc_header;
    }
    /* If it's not type 0, we already overwrote memory with other values, so don't check for initial pattern. */
    if (type == 0 && (*((uint32_t *)alloc) != 0x90909090)) {
        arch_irq_disable();
        co_printf("incorrect initial pattern (got %p)\n", *((uint32_t *)alloc));
        goto failed_with_alloc_header;
    }
    return alloc;
failed_with_alloc_header:
    co_printf(" - alloc header:\n");
    co_printf(" +-- region pointer: %p\n", alloc_header->pool);
    co_printf(" +-- block count:    %u\n", alloc_header->block_count);
    co_printf(" +-- data start:     %p\n", alloc_header->data);
failed:
    heap_free(alloc);
    return nullptr;
}

[[nodiscard]] static bool test_heap_alloc_and_fill(struct pool_header *self, size_t alloc_count, size_t alloc_block_count) {
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
                co_printf("[allocate&fill]");
            } else {
                co_printf(" [basic re-check]");
            }
        }
        for (size_t i = 0; i < alloc_count; i++) {
            char *alloc = test_heap_alloc(self, alloc_block_count, type, expected_bptr);
            if (alloc == nullptr) {
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
            co_printf("- alloc pointer:     %p\n", alloc);
            co_printf("- expected pointer:  %#p\n", expected_bptr);
            co_printf("- alloc index:       %u/%u\n", i, (alloc_count - 1));
            return false;
        }
    }
    return true;
}

[[nodiscard]] static bool test_heap_compare(struct pool_header *self, size_t alloc_count, size_t alloc_block_count) {
    size_t byte_count = byte_count_for_block_count(alloc_block_count);
    if (CONFIG_SEQUENTIAL_TEST_VERBOSE) {
        co_printf(" [compare]");
    }
    uint8_t *bptr = (UCHAR *)self->blockpool + sizeof(struct alloc_header);
    for (size_t i = 0; i < alloc_count; i++) {
        uint8_t *alloc = bptr;
        for (size_t j = 0; j < byte_count; j++) {
            if (alloc[j] != (i & 0xffU)) {
                co_printf("corrupted data\n");
                co_printf("- allocated at:      %p\n", bptr);
                co_printf("- alloc index:       %u/%u\n", i, (alloc_count - 1));
                co_printf("- byte offset:       %u/%u\n", j, byte_count - 1);
                co_printf("- expected:          %u\n", i);
                co_printf("- got:               %u\n", alloc[j]);
                return false;
            }
        }
        bptr += BLOCK_SIZE * alloc_block_count;
    }
    return true;
}

[[nodiscard]] static bool test_heap_free(struct pool_header *self, size_t alloc_count, size_t alloc_block_count) {
    size_t byte_count = byte_count_for_block_count(alloc_block_count);
    if (CONFIG_SEQUENTIAL_TEST_VERBOSE) {
        co_printf(" [free]");
    }
    uint8_t *bptr = (UCHAR *)self->blockpool + sizeof(struct alloc_header);
    for (size_t i = 0; i < alloc_count; i++) {
        UCHAR *alloc = bptr;
        heap_free(alloc);
        for (size_t j = 0; j < byte_count; j++) {
            if (alloc[j] != 0x6f) {
                co_printf("free pattern(0x6f) not found\n");
                co_printf("- allocated at:      %#lx\n", bptr);
                co_printf("- alloc index:       %u/%u\n", i, (alloc_count - 1));
                co_printf("- byte offset:       %u/%u\n", j, byte_count - 1);
                co_printf("- got:               %u\n", alloc[j]);
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
    co_printf("sequential memory test start(%#lx~%#lx, size %zu)\n", pool_start_addr, pool_end_addr, pool_end_addr - pool_start_addr + 1);

    while (1) {
        co_printf("- alloc byte count:  %u\n", byte_count_for_block_count(alloc_block_count));

        size_t alloc_count = self->block_count / alloc_block_count;
        if (alloc_count == 0) {
            break;
        }
        if (CONFIG_SEQUENTIAL_TEST_VERBOSE) {
            co_printf("* Testing %u x %u blocks: ", alloc_count, alloc_block_count);
        }
        if (!test_heap_alloc_and_fill(self, alloc_count, alloc_block_count)) {
            goto testfailed;
        }
        if (!test_heap_compare(self, alloc_count, alloc_block_count)) {
            goto testfailed;
        }
        if (!test_heap_free(self, alloc_count, alloc_block_count)) {
            goto testfailed;
        }
        alloc_block_count++;
        co_printf(" -> OK!\n");
        continue;
    testfailed:
        co_printf("- alloc block count: %u\n", alloc_block_count);
        co_printf("- pool start addr:   %#lx\n", pool_start_addr);
        co_printf("- pool end addr:     %#lx\n", pool_end_addr);
        return false;
    }
    co_printf("sequential memory test ok(%#lx~%#lx)\n", pool_start_addr, pool_end_addr - 1);
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
                return nullptr;
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
    /* If it is initial pool, and resulting pool block count is not enough, return nullptr; */
    if (!s_initialheapinitialized && (poolblock_count < size_to_blocks(minmemsize, BLOCK_SIZE))) {
        return nullptr;
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
    list_insert_back(&s_heappoollist, &pool->node, pool);

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
    size_t max_block_count = size_to_blocks(memsize, BLOCK_SIZE);

    size_t word_count = bitmap_needed_word_count(max_block_count);
    size_t metadata_size = sizeof(struct pool_header) + (word_count * sizeof(UINT));
    size_t maxsize = metadata_size + (max_block_count * BLOCK_SIZE);
    size_t page_count = size_to_blocks(maxsize, ARCH_PAGESIZE);
    struct pool_header *pool = mem;
    size_t bitmapsize = 0;
    size_t poolblock_count = 0;

    /* We have to make sure that total size doesn't exceed size of that buffer. */
    while (memsize < (page_count * ARCH_PAGESIZE)) {
        page_count--;
    }

    /* Now we calculate the actual size ***************************************/
    size_t totalblock_count = size_to_blocks(page_count * ARCH_PAGESIZE, BLOCK_SIZE);

    word_count = bitmap_needed_word_count(totalblock_count);
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
    vmemset(pool->blockbitmap.words, 0, pool->blockbitmap.word_count * sizeof(*pool->blockbitmap.words));
    bitmap_set_bits(&pool->blockbitmap, 0, poolblock_count);

    s_initial_heap_initialized = true;

    s_free_block_count += pool->block_count;
    list_insert_back(&s_heap_pool_list, &pool->node, pool);

    if (CONFIG_DO_POOL_SEQUENTIAL_TEST) {
        if (!test_heap(pool)) {
            panic("heap: sequential test failed");
        }
    }

    return pool;
}

void *heap_alloc(size_t size, uint8_t flags) {
    size_t actualsize = actual_alloc_size(size);
    size_t actualblock_count = size_to_blocks(actualsize, BLOCK_SIZE);

    if (size == 0) {
        return nullptr;
    }
    if ((SIZE_MAX - sizeof(struct alloc_header)) < size) {
        return nullptr;
    }
    bool prev_interrupts = arch_irq_disable();
    HEAP_CHECKOVERFLOW();
    if (!s_initial_heap_initialized) {
        add_mem(s_initial_heap_memory, sizeof(s_initial_heap_memory));
    }
    void *result = nullptr;
    if (actualblock_count < s_free_block_count) {
        LIST_FOREACH(&s_heap_pool_list, poolnode) {
            struct pool_header *pool = poolnode->data;
            assert(pool != nullptr);
            result = alloc_from_pool(pool, size);
            if (result != nullptr) {
                break;
            }
        }
    }
    HEAP_CHECKOVERFLOW();
    arch_irq_restore(prev_interrupts);
    if (flags & HEAP_FLAG_ZEROMEMORY) {
        vmemset(result, 0, size);
    }
    return result;
}

void heap_free(void *ptr) {
    if (ptr == nullptr) {
        return;
    }
    bool prev_interrupts = arch_irq_disable();
    struct alloc_header *alloc = alloc_header_of(ptr);
    HEAP_CHECKOVERFLOW();
    list_remove_node(&s_alloc_list, &alloc->node);
    if (alloc == nullptr) {
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
    bitmap_set_bits(&alloc->pool->blockbitmap, (long)blockindex, alloc->block_count);
    alloc->pool->usedblock_count -= alloc->block_count;
    s_free_block_count += alloc->block_count;
    vmemset(alloc, 0x6f, alloc->block_count * BLOCK_SIZE);
    HEAP_CHECKOVERFLOW();
    arch_irq_restore(prev_interrupts);
    return;
die:
    panic("heap_free: bad pointer");
}

void *heap_realloc(void *ptr, size_t newsize, uint8_t flags) {
    if (ptr == nullptr) {
        return heap_alloc(newsize, flags);
    }
    bool prev_interrupts = arch_irq_disable();
    struct alloc_header *alloc = alloc_header_of(ptr);
    HEAP_CHECKOVERFLOW();
    if (alloc == nullptr) {
        goto die;
    }
    size_t copysize = 0;
    if (newsize < alloc->size) {
        copysize = newsize;
    } else {
        copysize = alloc->size;
    }
    void *newmem = heap_alloc(newsize, flags);
    if (newmem == nullptr) {
        goto out;
    }
    vmemcpy(newmem, ptr, copysize);
    heap_free(ptr);
out:
    arch_irq_restore(prev_interrupts);
    return newmem;
die:
    panic("heap_realloc: bad pointer");
}

void *heap_calloc(size_t size, size_t elements, uint8_t flags) {
    if ((SIZE_MAX / size) < elements) {
        return nullptr;
    }
    return heap_alloc(size * elements, flags);
}

void *heap_realloc_array(void *ptr, size_t newsize, size_t newelements, uint8_t flags) {
    if ((SIZE_MAX / newsize) < newelements) {
        return nullptr;
    }
    return heap_realloc(ptr, newsize * newelements, flags);
}

static size_t const MAXEXPANDSIZE = 16 * 1024 * 1024;

void heap_expand(void) {
    bool prev_interrupts = arch_irq_disable();
    struct vmm_object *object = nullptr;
    size_t heapsize = pmm_get_total_mem_size();
    if (MAXEXPANDSIZE < heapsize) {
        heapsize = MAXEXPANDSIZE;
    }
    object = vmm_alloc(vmm_get_kernel_address_space(), heapsize, MAP_PROT_READ | MAP_PROT_WRITE);
    if (object == nullptr) {
        co_printf("not enough memory to expand heap\n");
        goto out;
    }
    add_mem(object->start, vmm_get_object_size((object)));
out:
    arch_irq_restore(prev_interrupts);
}

/******************************************************************************/

#define RAND_TEST_ALLOC_COUNT 10
#define MAX_ALLOC_SIZE ((1024 * 1024 * 2) / RAND_TEST_ALLOC_COUNT)

bool heap_run_random_test(void) {
    size_t allocsizes[RAND_TEST_ALLOC_COUNT];
    void **allocptrs[RAND_TEST_ALLOC_COUNT];

    for (size_t i = 0; i < RAND_TEST_ALLOC_COUNT; i++) {
        while (1) {
            allocsizes[i] = (size_t)rand() % MAX_ALLOC_SIZE;
            allocptrs[i] = heap_alloc(allocsizes[i], 0);
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
                co_printf("value mismatch at %p(allocation %zu, base %p, offset %zu): expected %p, got %p\n", &allocptrs[i][j], i, &allocptrs[i], j, expectedvalue, gotvalue);
                goto testfail;
            }
        }
    }
    for (size_t i = 0; i < RAND_TEST_ALLOC_COUNT; i++) {
        heap_free((void *)allocptrs[i]);
    }
    return true;
testfail:
    return false;
}
