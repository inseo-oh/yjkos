#include "../test.h"
#include <kernel/mem/heap.h>
#include <stddef.h>

static bool do_randalloc(void) {
    TEST_EXPECT(Heap_RunRandomTest());
    return true;
}

static bool do_badalloc(void) {
    TEST_EXPECT(Heap_Alloc(0, 0) == NULL);
    TEST_EXPECT(Heap_Alloc(~0U, 0) == NULL);
    return true;
}

static struct Test const TESTS[] = {
    { .name = "heap random test", .fn = do_randalloc },
    { .name = "bad Heap_Alloc",   .fn = do_badalloc  },
    /* TODO: Add tests for Calloc and ReallocArray */
};

const struct TestGroup TESTGROUP_HEAP = {
    .name = "heap",
    .tests = TESTS,
    .testslen = sizeof(TESTS)/sizeof(*TESTS),
};
