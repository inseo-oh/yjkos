#include "../test.h"
#include <kernel/arch/interrupts.h>
#include <kernel/io/co.h>
#include <kernel/tasks/mutex.h>
#include <kernel/tasks/sched.h>
#include <kernel/tasks/thread.h>
#include <stddef.h>

static bool do_basic(void) {
    struct Mutex mtx;
    Mutex_Init(&mtx);
    TEST_EXPECT(MUTEX_TRYLOCK(&mtx) == true);
    TEST_EXPECT(mtx.locked);
    TEST_EXPECT(MUTEX_TRYLOCK(&mtx) == false);
    Mutex_Unlock(&mtx);
    TEST_EXPECT(MUTEX_TRYLOCK(&mtx) == true);
    Mutex_Unlock(&mtx);
    return true;
}

struct sharedcontext {
    int cnt;
    struct Mutex mtx;
};

#define TEST_COUNTTARGET 100
#define TEST_THREADCOUNT 5

static void testthread(void *arg) {
    Arch_Irq_Enable();
    struct sharedcontext *ctx = arg;
    for (int i = 0; i < TEST_COUNTTARGET; i++) {
        MUTEX_LOCK(&ctx->mtx);
        int oldcnt = ctx->cnt;
        Sched_Schedule();
        if (ctx->cnt != oldcnt) {
            Co_Printf("shared var suddenly changed! expected: %d, got: %d\n", oldcnt, ctx->cnt);
        }
        ctx->cnt = oldcnt + 1;
        Mutex_Unlock(&ctx->mtx);
        Sched_Schedule();
    }
}

static bool do_threadsync(void) {
    bool result = false;
    struct sharedcontext ctx;
    Mutex_Init(&ctx.mtx);
    struct Thread *threads[TEST_THREADCOUNT];
    ctx.cnt = 0;
    for (int i = 0; i < TEST_THREADCOUNT; i++) {
        threads[i] = Thread_Create(THREAD_STACK_SIZE, testthread, &ctx);
        Co_Printf("created thread %p\n", threads[i]);
    }
    bool failed = false;
    for (int i = 0; i < TEST_THREADCOUNT; i++) {
        if (threads[i] == NULL) {
            Co_Printf("not enough memory to spawn threads\n");
            goto out;
        }
        int ret = Sched_Queue(threads[i]);
        if (ret < 0) {
            Co_Printf("failed to queue thread (error %d)\n", ret);
            Thread_Delete(threads[i]);
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
        Co_Printf("\r%d", ctx.cnt);
        Mutex_Unlock(&ctx.mtx);
        Sched_Schedule();
        if (done) {
            break;
        }
    }
    Co_Printf("\n", ctx.cnt);
    result = true;
out:
    Co_Printf("shutting down...\n");
    for (int i = 0; i < TEST_THREADCOUNT; i++) {
        if (threads[i] != NULL) {
            threads[i]->shutdown = true;
        }
    }
    return result;
}

static struct Test const TESTS[] = {
    {.name = "basic lock & unlock test", .fn = do_basic},
    {.name = "thread synchronization", .fn = do_threadsync},
};

const struct TestGroup TESTGROUP_MUTEX = {
    .name = "mutex",
    .tests = TESTS,
    .testslen = sizeof(TESTS) / sizeof(*TESTS),
};
