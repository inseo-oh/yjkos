#include "../test.h"
#include <kernel/mem/heap.h>
#include <stddef.h>

static bool do_randalloc(void) {
    TEST_EXPECT(heap_run_random_test());
    return true;
}

static bool do_badalloc(void) {
    TEST_EXPECT(heap_alloc(0, 0) == nullptr);
    TEST_EXPECT(heap_alloc(~0U, 0) == nullptr);
    return true;
}

static struct test const TESTS[] = {
    { .name = "heap random test", .fn = do_randalloc },
    { .name = "bad heap_alloc",   .fn = do_badalloc  },
    /* TODO: Add tests for Calloc and ReallocArray */
};

const struct test_group TESTGROUP_HEAP = {
    .name = "heap",
    .tests = TESTS,
    .testslen = sizeof(TESTS)/sizeof(*TESTS),
};
