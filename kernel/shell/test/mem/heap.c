#include "../../shell.h"
#include "../test.h"
#include <kernel/mem/heap.h>
#include <stddef.h>

SHELLFUNC static bool do_randalloc(void) {
    TEST_EXPECT(heap_run_random_test());
    return true;
}

SHELLFUNC static bool do_badalloc(void) {
    TEST_EXPECT(heap_alloc(0, 0) == NULL);
    TEST_EXPECT(heap_alloc(~0, 0) == NULL);
    return true;
}

SHELLRODATA static struct test const TESTS[] = {
    { .name = "heap random test", .fn = do_randalloc },
    { .name = "bad heap_alloc",   .fn = do_badalloc  },
    // TODO: Add tests for calloc and reallocarray
};

SHELLRODATA const struct testgroup TESTGROUP_HEAP = {
    .name = "heap",
    .tests = TESTS,
    .testslen = sizeof(TESTS)/sizeof(*TESTS),
};
