#include "../test.h"
#include <kernel/arch/interrupts.h>
#include <kernel/io/co.h>
#include <kernel/tasks/mutex.h>
#include <kernel/tasks/sched.h>
#include <kernel/tasks/thread.h>
#include <stddef.h>

static bool do_basic(void) {
    struct mutex mtx;
    mutex_init(&mtx);
    TEST_EXPECT(MUTEX_TRYLOCK(&mtx) == true);
    TEST_EXPECT(mtx.locked);
    TEST_EXPECT(MUTEX_TRYLOCK(&mtx) == false);
    mutex_unlock(&mtx);
    TEST_EXPECT(MUTEX_TRYLOCK(&mtx) == true);
    mutex_unlock(&mtx);
    return true;
}

struct sharedcontext {
    int cnt;
    struct mutex mtx;
};

#define TEST_COUNTTARGET 100
#define TEST_THREADCOUNT 5

static void testthread(void *arg) {
    arch_irq_enable();
    struct sharedcontext *ctx = arg;
    for (int i = 0; i < TEST_COUNTTARGET; i++) {
        MUTEX_LOCK(&ctx->mtx);
        int oldcnt = ctx->cnt;
        sched_schedule();
        if (ctx->cnt != oldcnt) {
            co_printf("shared var suddenly changed! expected: %d, got: %d\n", oldcnt, ctx->cnt);
        }
        ctx->cnt = oldcnt + 1;
        mutex_unlock(&ctx->mtx);
        sched_schedule();
    }
}

static bool do_threadsync(void) {
    bool result = false;
    struct sharedcontext ctx;
    mutex_init(&ctx.mtx);
    struct thread *threads[TEST_THREADCOUNT];
    ctx.cnt = 0;
    for (int i = 0; i < TEST_THREADCOUNT; i++) {
        threads[i] = thread_create(THREAD_STACK_SIZE, testthread, &ctx);
        co_printf("created thread %p\n", threads[i]);
    }
    bool failed = false;
    for (int i = 0; i < TEST_THREADCOUNT; i++) {
        if (threads[i] == NULL) {
            co_printf("not enough memory to spawn threads\n");
            goto out;
        }
        int ret = sched_queue(threads[i]);
        if (ret < 0) {
            co_printf("failed to queue thread (error %d)\n", ret);
            thread_delete(threads[i]);
            threads[i] = NULL;
            failed = true;
        }
    }
    if (failed) {
        goto out;
    }
    while (1) {
        MUTEX_LOCK(&ctx.mtx);
        bool done = (TEST_COUNTTARGET * TEST_THREADCOUNT) <= ctx.cnt;
        co_printf("\r%d", ctx.cnt);
        mutex_unlock(&ctx.mtx);
        sched_schedule();
        if (done) {
            break;
        }
    }
    co_printf("\n", ctx.cnt);
    result = true;
out:
    co_printf("shutting down...\n");
    for (int i = 0; i < TEST_THREADCOUNT; i++) {
        if (threads[i] != NULL) {
            threads[i]->shutdown = true;
        }
    }
    return result;
}

static struct test const TESTS[] = {
    {.name = "basic lock & unlock test", .fn = do_basic},
    {.name = "thread synchronization", .fn = do_threadsync},
};

const struct test_group TESTGROUP_MUTEX = {
    .name = "mutex",
    .tests = TESTS,
    .testslen = sizeof(TESTS) / sizeof(*TESTS),
};
