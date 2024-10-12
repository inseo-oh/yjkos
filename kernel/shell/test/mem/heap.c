#include "../../shell.h"
#include "../test.h"
#include <kernel/mem/heap.h>
#include <stddef.h>

SHELLFUNC static testresult_t do_randalloc(void) {
    TEST_EXPECT(heap_run_random_test());
    return TEST_OK;
}

SHELLFUNC static testresult_t do_badalloc(void) {
    TEST_EXPECT(heap_alloc(0, 0) == NULL);
    TEST_EXPECT(heap_alloc(~0, 0) == NULL);
    return TEST_OK;
}

SHELLRODATA static struct test const TESTS[] = {
    { .name = "heap random test", .fn = do_randalloc },
    { .name = "bad heap_alloc",   .fn = do_badalloc  },
    // TODO: Add tests for calloc and reallocarray
};

SHELLDATA const struct testgroup TESTGROUP_HEAP = {
    .name = "heap",
    .tests = TESTS,
    .testslen = sizeof(TESTS)/sizeof(*TESTS),
};
