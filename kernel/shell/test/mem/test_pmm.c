#include "../test.h"
#include <kernel/arch/interrupts.h>
#include <kernel/mem/pmm.h>
#include <kernel/types.h>

static bool do_randalloc(void) {
    bool prev_interrupts = arch_interrupts_disable();
    TEST_EXPECT(pmm_pagepool_test_random());
    interrupts_restore(prev_interrupts);
    return true;
}

static bool do_badalloc(void) {
    bool prev_interrupts = arch_interrupts_disable();
    size_t page_count = ~0U;
    TEST_EXPECT(pmm_alloc(&page_count) == PHYSICALPTR_NULL);
    interrupts_restore(prev_interrupts);
    return true;
}

static struct test const TESTS[] = {
    { .name = "random allocation test", .fn = do_randalloc },
    { .name = "bad allocation",         .fn = do_badalloc  },
};

const struct testgroup TESTGROUP_PMM = {
    .name = "pmm",
    .tests = TESTS,
    .testslen = sizeof(TESTS)/sizeof(*TESTS),
};
