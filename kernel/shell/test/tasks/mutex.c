#include "../test.h"
#include <kernel/tasks/mutex.h>

static bool do_basic(void) {
    struct mutex mtx;
    mutex_init(&mtx);
    TEST_EXPECT(mutex_trylock(&mtx) == true);
    TEST_EXPECT(mtx.locked);
    TEST_EXPECT(mutex_trylock(&mtx) == false);
    mutex_unlock(&mtx);
    TEST_EXPECT(mutex_trylock(&mtx) == true);
    mutex_unlock(&mtx);
    return true;
}

static struct test const TESTS[] = {
    { .name = "basic lock & unlock test", .fn = do_basic },
};

const struct testgroup TESTGROUP_MUTEX = {
    .name = "mutex",
    .tests = TESTS,
    .testslen = sizeof(TESTS)/sizeof(*TESTS),
};

