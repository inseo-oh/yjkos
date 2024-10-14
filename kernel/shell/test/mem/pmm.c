#include "../../shell.h"
#include "../test.h"
#include <kernel/arch/interrupts.h>
#include <kernel/mem/pmm.h>
#include <kernel/types.h>

SHELLFUNC static bool do_randalloc(void) {
    bool previnterrupts = arch_interrupts_disable();
    TEST_EXPECT(pmm_pagepool_test_random());
    interrupts_restore(previnterrupts);
    return true;
}

SHELLFUNC static bool do_badalloc(void) {
    bool previnterrupts = arch_interrupts_disable();
    size_t pagecount = ~0;
    TEST_EXPECT(pmm_alloc(&pagecount) == PHYSICALPTR_NULL);
    interrupts_restore(previnterrupts);
    return true;
}

SHELLRODATA static struct test const TESTS[] = {
    { .name = "random allocation test", .fn = do_randalloc },
    { .name = "bad allocation",         .fn = do_badalloc  },
};

SHELLRODATA const struct testgroup TESTGROUP_PMM = {
    .name = "pmm",
    .tests = TESTS,
    .testslen = sizeof(TESTS)/sizeof(*TESTS),
};
