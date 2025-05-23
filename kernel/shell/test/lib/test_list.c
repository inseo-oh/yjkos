#include "../test.h"
#include <kernel/lib/list.h>
#include <kernel/lib/strutil.h>

static bool do_insertfront(void) {
    struct list lst;
    struct list_node nodes[3];

    vmemset(&lst, 0x55, sizeof(lst));
    vmemset(nodes, 0x55, sizeof(nodes));
    list_init(&lst);

    list_insert_front(&lst, &nodes[0], nullptr);
    TEST_EXPECT(lst.front == &nodes[0]);
    TEST_EXPECT(lst.back == &nodes[0]);
    TEST_EXPECT(nodes[0].prev == nullptr);
    TEST_EXPECT(nodes[0].next == nullptr);

    list_insert_front(&lst, &nodes[1], nullptr);
    TEST_EXPECT(lst.front == &nodes[1]);
    TEST_EXPECT(lst.back == &nodes[0]);
    TEST_EXPECT(nodes[1].prev == nullptr);
    TEST_EXPECT(nodes[1].next == &nodes[0]);
    TEST_EXPECT(nodes[0].prev == &nodes[1]);
    TEST_EXPECT(nodes[0].next == nullptr);

    list_insert_front(&lst, &nodes[2], nullptr);
    TEST_EXPECT(lst.front == &nodes[2]);
    TEST_EXPECT(lst.back == &nodes[0]);
    TEST_EXPECT(nodes[2].prev == nullptr);
    TEST_EXPECT(nodes[2].next == &nodes[1]);
    TEST_EXPECT(nodes[1].prev == &nodes[2]);
    TEST_EXPECT(nodes[1].next == &nodes[0]);
    TEST_EXPECT(nodes[0].prev == &nodes[1]);
    TEST_EXPECT(nodes[0].next == nullptr);

    return true;
}

static bool do_insertback(void) {
    struct list lst;
    struct list_node nodes[3];

    vmemset(nodes, 0x55, sizeof(nodes));
    list_init(&lst);

    list_insert_back(&lst, &nodes[0], nullptr);
    TEST_EXPECT(lst.front == &nodes[0]);
    TEST_EXPECT(lst.back == &nodes[0]);
    TEST_EXPECT(nodes[0].prev == nullptr);
    TEST_EXPECT(nodes[0].next == nullptr);

    list_insert_back(&lst, &nodes[1], nullptr);
    TEST_EXPECT(lst.front == &nodes[0]);
    TEST_EXPECT(lst.back == &nodes[1]);
    TEST_EXPECT(nodes[0].prev == nullptr);
    TEST_EXPECT(nodes[0].next == &nodes[1]);
    TEST_EXPECT(nodes[1].prev == &nodes[0]);
    TEST_EXPECT(nodes[1].next == nullptr);

    list_insert_back(&lst, &nodes[2], nullptr);
    TEST_EXPECT(lst.front == &nodes[0]);
    TEST_EXPECT(lst.back == &nodes[2]);
    TEST_EXPECT(nodes[0].prev == nullptr);
    TEST_EXPECT(nodes[0].next == &nodes[1]);
    TEST_EXPECT(nodes[1].prev == &nodes[0]);
    TEST_EXPECT(nodes[1].next == &nodes[2]);
    TEST_EXPECT(nodes[2].prev == &nodes[1]);
    TEST_EXPECT(nodes[2].next == nullptr);

    return true;
}

static bool do_insertafter(void) {
    struct list lst;
    struct list_node nodes[5];

    vmemset(nodes, 0x55, sizeof(nodes));
    list_init(&lst);

    list_insert_back(&lst, &nodes[0], nullptr);
    list_insert_back(&lst, &nodes[1], nullptr);
    list_insert_back(&lst, &nodes[2], nullptr);

    list_insert_after(&lst, &nodes[1], &nodes[3], nullptr);
    TEST_EXPECT(lst.front == &nodes[0]);
    TEST_EXPECT(lst.back == &nodes[2]);
    TEST_EXPECT(nodes[1].prev == &nodes[0]);
    TEST_EXPECT(nodes[1].next == &nodes[3]);
    TEST_EXPECT(nodes[3].prev == &nodes[1]);
    TEST_EXPECT(nodes[3].next == &nodes[2]);
    TEST_EXPECT(nodes[2].prev == &nodes[3]);
    TEST_EXPECT(nodes[2].next == nullptr);

    list_insert_after(&lst, &nodes[2], &nodes[4], nullptr);
    TEST_EXPECT(lst.front == &nodes[0]);
    TEST_EXPECT(lst.back == &nodes[4]);
    TEST_EXPECT(nodes[2].prev == &nodes[3]);
    TEST_EXPECT(nodes[2].next == &nodes[4]);
    TEST_EXPECT(nodes[4].prev == &nodes[2]);
    TEST_EXPECT(nodes[4].next == nullptr);

    return true;
}

static bool do_insertbefore(void) {
    struct list lst;
    struct list_node nodes[5];

    vmemset(nodes, 0x55, sizeof(nodes));
    list_init(&lst);

    list_insert_back(&lst, &nodes[0], nullptr);
    list_insert_back(&lst, &nodes[1], nullptr);
    list_insert_back(&lst, &nodes[2], nullptr);

    list_insert_before(&lst, &nodes[1], &nodes[3], nullptr);
    TEST_EXPECT(lst.front == &nodes[0]);
    TEST_EXPECT(lst.back == &nodes[2]);
    TEST_EXPECT(nodes[0].prev == nullptr);
    TEST_EXPECT(nodes[0].next == &nodes[3]);
    TEST_EXPECT(nodes[3].prev == &nodes[0]);
    TEST_EXPECT(nodes[3].next == &nodes[1]);
    TEST_EXPECT(nodes[1].prev == &nodes[3]);
    TEST_EXPECT(nodes[1].next == &nodes[2]);

    list_insert_before(&lst, &nodes[0], &nodes[4], nullptr);
    TEST_EXPECT(lst.front == &nodes[4]);
    TEST_EXPECT(lst.back == &nodes[2]);
    TEST_EXPECT(nodes[4].prev == nullptr);
    TEST_EXPECT(nodes[4].next == &nodes[0]);
    TEST_EXPECT(nodes[0].prev == &nodes[4]);
    TEST_EXPECT(nodes[0].next == &nodes[3]);

    return true;
}

static bool do_removefront(void) {
    struct list lst;
    struct list_node nodes[3];
    struct list_node *removednode = nullptr;

    vmemset(nodes, 0x55, sizeof(nodes));
    list_init(&lst);

    list_insert_back(&lst, &nodes[0], nullptr);
    list_insert_back(&lst, &nodes[1], nullptr);
    list_insert_back(&lst, &nodes[2], nullptr);

    removednode = list_remove_front(&lst);
    TEST_EXPECT(removednode == &nodes[0]);
    TEST_EXPECT(lst.front == &nodes[1]);
    TEST_EXPECT(lst.back == &nodes[2]);
    TEST_EXPECT(nodes[1].prev == nullptr);
    TEST_EXPECT(nodes[1].next == &nodes[2]);
    TEST_EXPECT(nodes[2].prev == &nodes[1]);
    TEST_EXPECT(nodes[2].next == nullptr);

    removednode = list_remove_front(&lst);
    TEST_EXPECT(removednode == &nodes[1]);
    TEST_EXPECT(lst.front == &nodes[2]);
    TEST_EXPECT(lst.back == &nodes[2]);
    TEST_EXPECT(nodes[2].prev == nullptr);
    TEST_EXPECT(nodes[2].next == nullptr);

    removednode = list_remove_front(&lst);
    TEST_EXPECT(removednode == &nodes[2]);
    TEST_EXPECT(lst.front == nullptr);
    TEST_EXPECT(lst.back == nullptr);

    removednode = list_remove_front(&lst);
    TEST_EXPECT(removednode == nullptr);
    return true;
}

static bool do_removeback(void) {
    struct list lst;
    struct list_node nodes[3];
    struct list_node *removednode = nullptr;

    vmemset(nodes, 0x55, sizeof(nodes));
    list_init(&lst);

    list_insert_back(&lst, &nodes[0], nullptr);
    list_insert_back(&lst, &nodes[1], nullptr);
    list_insert_back(&lst, &nodes[2], nullptr);

    removednode = list_remove_back(&lst);
    TEST_EXPECT(removednode == &nodes[2]);
    TEST_EXPECT(lst.front == &nodes[0]);
    TEST_EXPECT(lst.back == &nodes[1]);
    TEST_EXPECT(nodes[0].prev == nullptr);
    TEST_EXPECT(nodes[0].next == &nodes[1]);
    TEST_EXPECT(nodes[1].prev == &nodes[0]);
    TEST_EXPECT(nodes[1].next == nullptr);

    removednode = list_remove_back(&lst);
    TEST_EXPECT(removednode == &nodes[1]);
    TEST_EXPECT(lst.front == &nodes[0]);
    TEST_EXPECT(lst.back == &nodes[0]);
    TEST_EXPECT(nodes[0].prev == nullptr);
    TEST_EXPECT(nodes[0].next == nullptr);

    removednode = list_remove_back(&lst);
    TEST_EXPECT(removednode == &nodes[0]);
    TEST_EXPECT(lst.front == nullptr);
    TEST_EXPECT(lst.back == nullptr);

    removednode = list_remove_back(&lst);
    TEST_EXPECT(removednode == nullptr);
    return true;
}

static bool do_removenode(void) {
    struct list lst;
    struct list_node nodes[3];

    vmemset(nodes, 0x55, sizeof(nodes));
    list_init(&lst);

    list_insert_back(&lst, &nodes[0], nullptr);
    list_insert_back(&lst, &nodes[1], nullptr);
    list_insert_back(&lst, &nodes[2], nullptr);

    list_remove_node(&lst, &nodes[1]);
    TEST_EXPECT(lst.front == &nodes[0]);
    TEST_EXPECT(lst.back == &nodes[2]);
    TEST_EXPECT(nodes[0].prev == nullptr);
    TEST_EXPECT(nodes[0].next == &nodes[2]);
    TEST_EXPECT(nodes[2].prev == &nodes[0]);
    TEST_EXPECT(nodes[2].next == nullptr);

    list_remove_node(&lst, &nodes[0]);
    TEST_EXPECT(lst.front == &nodes[2]);
    TEST_EXPECT(lst.back == &nodes[2]);
    TEST_EXPECT(nodes[2].prev == nullptr);
    TEST_EXPECT(nodes[2].next == nullptr);

    list_remove_node(&lst, &nodes[2]);
    TEST_EXPECT(lst.front == nullptr);
    TEST_EXPECT(lst.back == nullptr);

    return true;
}

static struct test const TESTS[] = {
    {.name = "insert front", .fn = do_insertfront},
    {.name = "insert back", .fn = do_insertback},
    {.name = "insert after", .fn = do_insertafter},
    {.name = "insert before", .fn = do_insertbefore},
    {.name = "remove front", .fn = do_removefront},
    {.name = "remove back", .fn = do_removeback},
    {.name = "remove node", .fn = do_removenode},
};

const struct test_group TESTGROUP_LIST = {
    .name = "list",
    .tests = TESTS,
    .testslen = sizeof(TESTS) / sizeof(*TESTS),
};
