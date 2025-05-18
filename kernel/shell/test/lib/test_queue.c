#include "../test.h"
#include <errno.h>
#include <kernel/lib/queue.h>
#include <stdint.h>

static uint32_t const TEST_INTS[] = {
    0x47bd8fbc,
    0x051b34b6,
    0x305c5756,
    0xd733129a,
    0xc4ad1efc,
    0x6d00295f,
    0x3c769a6e,
    0x1e9d30e8,
    0x373be348,
    0xe80d6aa0,
};

static bool do_test(void) {
    struct Queue queue;
    uint32_t buf[5];
    QUEUE_INIT_FOR_ARRAY(&queue, buf);
    TEST_EXPECT(QUEUE_ENQUEUE(&queue, &TEST_INTS[0]) == 0);
    TEST_EXPECT(QUEUE_ENQUEUE(&queue, &TEST_INTS[1]) == 0);
    TEST_EXPECT(QUEUE_ENQUEUE(&queue, &TEST_INTS[2]) == 0);
    TEST_EXPECT(QUEUE_ENQUEUE(&queue, &TEST_INTS[3]) == 0);
    TEST_EXPECT(QUEUE_ENQUEUE(&queue, &TEST_INTS[4]) == 0);
    TEST_EXPECT(QUEUE_ENQUEUE(&queue, &TEST_INTS[5]) == -ENOMEM);
    uint32_t dequeued = false;
    TEST_EXPECT(QUEUE_DEQUEUE(&dequeued, &queue));
    TEST_EXPECT(dequeued == TEST_INTS[0]);
    TEST_EXPECT(QUEUE_DEQUEUE(&dequeued, &queue));
    TEST_EXPECT(dequeued == TEST_INTS[1]);
    TEST_EXPECT(QUEUE_DEQUEUE(&dequeued, &queue));
    TEST_EXPECT(dequeued == TEST_INTS[2]);
    TEST_EXPECT(QUEUE_ENQUEUE(&queue, &TEST_INTS[5]) == 0);
    TEST_EXPECT(QUEUE_ENQUEUE(&queue, &TEST_INTS[6]) == 0);
    TEST_EXPECT(QUEUE_ENQUEUE(&queue, &TEST_INTS[7]) == 0);
    TEST_EXPECT(QUEUE_DEQUEUE(&dequeued, &queue));
    TEST_EXPECT(dequeued == TEST_INTS[3]);
    TEST_EXPECT(QUEUE_DEQUEUE(&dequeued, &queue));
    TEST_EXPECT(dequeued == TEST_INTS[4]);
    TEST_EXPECT(QUEUE_DEQUEUE(&dequeued, &queue));
    TEST_EXPECT(dequeued == TEST_INTS[5]);
    TEST_EXPECT(QUEUE_DEQUEUE(&dequeued, &queue));
    TEST_EXPECT(dequeued == TEST_INTS[6]);
    TEST_EXPECT(QUEUE_DEQUEUE(&dequeued, &queue));
    TEST_EXPECT(dequeued == TEST_INTS[7]);
    TEST_EXPECT(!QUEUE_DEQUEUE(&dequeued, &queue));

    return true;
}

static struct Test const TESTS[] = {
    {.name = "queue", .fn = do_test},
};

const struct TestGroup TESTGROUP_QUEUE = {
    .name = "queue",
    .tests = TESTS,
    .testslen = sizeof(TESTS) / sizeof(*TESTS),
};
