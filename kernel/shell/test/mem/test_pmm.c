#include "../test.h"
#include <kernel/arch/interrupts.h>
#include <kernel/mem/pmm.h>
#include <kernel/types.h>

static bool do_randalloc(void) {
    bool prev_interrupts = Arch_Irq_Disable();
    TEST_EXPECT(Pmm_PagePoolTestRandom());
    Arch_Irq_Restore(prev_interrupts);
    return true;
}

static bool do_badalloc(void) {
    bool prev_interrupts = Arch_Irq_Disable();
    size_t page_count = ~0U;
    TEST_EXPECT(Pmm_Alloc(&page_count) == PHYSICALPTR_NULL);
    Arch_Irq_Restore(prev_interrupts);
    return true;
}

static struct Test const TESTS[] = {
    { .name = "random allocation test", .fn = do_randalloc },
    { .name = "bad allocation",         .fn = do_badalloc  },
};

const struct TestGroup TESTGROUP_PMM = {
    .name = "pmm",
    .tests = TESTS,
    .testslen = sizeof(TESTS)/sizeof(*TESTS),
};
