#include "../test.h"
#include <kernel/mem/heap.h>
#include <stddef.h>

static bool do_randalloc(void) {
    TEST_EXPECT(heap_run_random_test());
    return true;
}

static bool do_badalloc(void) {
    TEST_EXPECT(heap_alloc(0, 0) == NULL);
    TEST_EXPECT(heap_alloc(~0U, 0) == NULL);
    return true;
}

static struct test const TESTS[] = {
    { .name = "heap random test", .fn = do_randalloc },
    { .name = "bad heap_alloc",   .fn = do_badalloc  },
    // TODO: Add tests for calloc and reallocarray
};

const struct testgroup TESTGROUP_HEAP = {
    .name = "heap",
    .tests = TESTS,
    .testslen = sizeof(TESTS)/sizeof(*TESTS),
};
