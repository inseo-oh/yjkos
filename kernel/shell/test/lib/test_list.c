#include "../test.h"
#include <kernel/lib/list.h>
#include <string.h>

static bool do_insertfront(void) {
    struct List lst;
    struct List_Node nodes[3];

    memset(&lst, 0x55, sizeof(lst));
    memset(nodes, 0x55, sizeof(nodes));
    List_Init(&lst);

    List_InsertFront(&lst, &nodes[0], NULL);
    TEST_EXPECT(lst.front == &nodes[0]);
    TEST_EXPECT(lst.back == &nodes[0]);
    TEST_EXPECT(nodes[0].prev == NULL);
    TEST_EXPECT(nodes[0].next == NULL);

    List_InsertFront(&lst, &nodes[1], NULL);
    TEST_EXPECT(lst.front == &nodes[1]);
    TEST_EXPECT(lst.back == &nodes[0]);
    TEST_EXPECT(nodes[1].prev == NULL);
    TEST_EXPECT(nodes[1].next == &nodes[0]);
    TEST_EXPECT(nodes[0].prev == &nodes[1]);
    TEST_EXPECT(nodes[0].next == NULL);

    List_InsertFront(&lst, &nodes[2], NULL);
    TEST_EXPECT(lst.front == &nodes[2]);
    TEST_EXPECT(lst.back == &nodes[0]);
    TEST_EXPECT(nodes[2].prev == NULL);
    TEST_EXPECT(nodes[2].next == &nodes[1]);
    TEST_EXPECT(nodes[1].prev == &nodes[2]);
    TEST_EXPECT(nodes[1].next == &nodes[0]);
    TEST_EXPECT(nodes[0].prev == &nodes[1]);
    TEST_EXPECT(nodes[0].next == NULL);

    return true;
}

static bool do_insertback(void) {
    struct List lst;
    struct List_Node nodes[3];

    memset(nodes, 0x55, sizeof(nodes));
    List_Init(&lst);

    List_InsertBack(&lst, &nodes[0], NULL);
    TEST_EXPECT(lst.front == &nodes[0]);
    TEST_EXPECT(lst.back == &nodes[0]);
    TEST_EXPECT(nodes[0].prev == NULL);
    TEST_EXPECT(nodes[0].next == NULL);

    List_InsertBack(&lst, &nodes[1], NULL);
    TEST_EXPECT(lst.front == &nodes[0]);
    TEST_EXPECT(lst.back == &nodes[1]);
    TEST_EXPECT(nodes[0].prev == NULL);
    TEST_EXPECT(nodes[0].next == &nodes[1]);
    TEST_EXPECT(nodes[1].prev == &nodes[0]);
    TEST_EXPECT(nodes[1].next == NULL);

    List_InsertBack(&lst, &nodes[2], NULL);
    TEST_EXPECT(lst.front == &nodes[0]);
    TEST_EXPECT(lst.back == &nodes[2]);
    TEST_EXPECT(nodes[0].prev == NULL);
    TEST_EXPECT(nodes[0].next == &nodes[1]);
    TEST_EXPECT(nodes[1].prev == &nodes[0]);
    TEST_EXPECT(nodes[1].next == &nodes[2]);
    TEST_EXPECT(nodes[2].prev == &nodes[1]);
    TEST_EXPECT(nodes[2].next == NULL);

    return true;
}

static bool do_insertafter(void) {
    struct List lst;
    struct List_Node nodes[5];

    memset(nodes, 0x55, sizeof(nodes));
    List_Init(&lst);

    List_InsertBack(&lst, &nodes[0], NULL);
    List_InsertBack(&lst, &nodes[1], NULL);
    List_InsertBack(&lst, &nodes[2], NULL);

    List_InsertAfter(&lst, &nodes[1], &nodes[3], NULL);
    TEST_EXPECT(lst.front == &nodes[0]);
    TEST_EXPECT(lst.back == &nodes[2]);
    TEST_EXPECT(nodes[1].prev == &nodes[0]);
    TEST_EXPECT(nodes[1].next == &nodes[3]);
    TEST_EXPECT(nodes[3].prev == &nodes[1]);
    TEST_EXPECT(nodes[3].next == &nodes[2]);
    TEST_EXPECT(nodes[2].prev == &nodes[3]);
    TEST_EXPECT(nodes[2].next == NULL);

    List_InsertAfter(&lst, &nodes[2], &nodes[4], NULL);
    TEST_EXPECT(lst.front == &nodes[0]);
    TEST_EXPECT(lst.back == &nodes[4]);
    TEST_EXPECT(nodes[2].prev == &nodes[3]);
    TEST_EXPECT(nodes[2].next == &nodes[4]);
    TEST_EXPECT(nodes[4].prev == &nodes[2]);
    TEST_EXPECT(nodes[4].next == NULL);

    return true;
}

static bool do_insertbefore(void) {
    struct List lst;
    struct List_Node nodes[5];

    memset(nodes, 0x55, sizeof(nodes));
    List_Init(&lst);

    List_InsertBack(&lst, &nodes[0], NULL);
    List_InsertBack(&lst, &nodes[1], NULL);
    List_InsertBack(&lst, &nodes[2], NULL);

    List_InsertBefore(&lst, &nodes[1], &nodes[3], NULL);
    TEST_EXPECT(lst.front == &nodes[0]);
    TEST_EXPECT(lst.back == &nodes[2]);
    TEST_EXPECT(nodes[0].prev == NULL);
    TEST_EXPECT(nodes[0].next == &nodes[3]);
    TEST_EXPECT(nodes[3].prev == &nodes[0]);
    TEST_EXPECT(nodes[3].next == &nodes[1]);
    TEST_EXPECT(nodes[1].prev == &nodes[3]);
    TEST_EXPECT(nodes[1].next == &nodes[2]);

    List_InsertBefore(&lst, &nodes[0], &nodes[4], NULL);
    TEST_EXPECT(lst.front == &nodes[4]);
    TEST_EXPECT(lst.back == &nodes[2]);
    TEST_EXPECT(nodes[4].prev == NULL);
    TEST_EXPECT(nodes[4].next == &nodes[0]);
    TEST_EXPECT(nodes[0].prev == &nodes[4]);
    TEST_EXPECT(nodes[0].next == &nodes[3]);

    return true;
}

static bool do_removefront(void) {
    struct List lst;
    struct List_Node nodes[3];
    struct List_Node *removednode = NULL;

    memset(nodes, 0x55, sizeof(nodes));
    List_Init(&lst);

    List_InsertBack(&lst, &nodes[0], NULL);
    List_InsertBack(&lst, &nodes[1], NULL);
    List_InsertBack(&lst, &nodes[2], NULL);
    
    removednode = List_RemoveFront(&lst);
    TEST_EXPECT(removednode == &nodes[0]);
    TEST_EXPECT(lst.front == &nodes[1]);
    TEST_EXPECT(lst.back == &nodes[2]);
    TEST_EXPECT(nodes[1].prev == NULL);
    TEST_EXPECT(nodes[1].next == &nodes[2]);
    TEST_EXPECT(nodes[2].prev == &nodes[1]);
    TEST_EXPECT(nodes[2].next == NULL);

    removednode = List_RemoveFront(&lst);
    TEST_EXPECT(removednode == &nodes[1]);
    TEST_EXPECT(lst.front == &nodes[2]);
    TEST_EXPECT(lst.back == &nodes[2]);
    TEST_EXPECT(nodes[2].prev == NULL);
    TEST_EXPECT(nodes[2].next == NULL);

    removednode = List_RemoveFront(&lst);
    TEST_EXPECT(removednode == &nodes[2]);
    TEST_EXPECT(lst.front == NULL);
    TEST_EXPECT(lst.back == NULL);

    removednode = List_RemoveFront(&lst);
    TEST_EXPECT(removednode == NULL);
    return true;
}

static bool do_removeback(void) {
    struct List lst;
    struct List_Node nodes[3];
    struct List_Node *removednode = NULL;

    memset(nodes, 0x55, sizeof(nodes));
    List_Init(&lst);

    List_InsertBack(&lst, &nodes[0], NULL);
    List_InsertBack(&lst, &nodes[1], NULL);
    List_InsertBack(&lst, &nodes[2], NULL);

    removednode = List_RemoveBack(&lst);
    TEST_EXPECT(removednode == &nodes[2]);
    TEST_EXPECT(lst.front == &nodes[0]);
    TEST_EXPECT(lst.back == &nodes[1]);
    TEST_EXPECT(nodes[0].prev == NULL);
    TEST_EXPECT(nodes[0].next == &nodes[1]);
    TEST_EXPECT(nodes[1].prev == &nodes[0]);
    TEST_EXPECT(nodes[1].next == NULL);

    removednode = List_RemoveBack(&lst);
    TEST_EXPECT(removednode == &nodes[1]);
    TEST_EXPECT(lst.front == &nodes[0]);
    TEST_EXPECT(lst.back == &nodes[0]);
    TEST_EXPECT(nodes[0].prev == NULL);
    TEST_EXPECT(nodes[0].next == NULL);

    removednode = List_RemoveBack(&lst);
    TEST_EXPECT(removednode == &nodes[0]);
    TEST_EXPECT(lst.front == NULL);
    TEST_EXPECT(lst.back == NULL);

    removednode = List_RemoveBack(&lst);
    TEST_EXPECT(removednode == NULL);
    return true;
}

static bool do_removenode(void) {
    struct List lst;
    struct List_Node nodes[3];

    memset(nodes, 0x55, sizeof(nodes));
    List_Init(&lst);

    List_InsertBack(&lst, &nodes[0], NULL);
    List_InsertBack(&lst, &nodes[1], NULL);
    List_InsertBack(&lst, &nodes[2], NULL);
    
    List_RemoveNode(&lst, &nodes[1]);
    TEST_EXPECT(lst.front == &nodes[0]);
    TEST_EXPECT(lst.back == &nodes[2]);
    TEST_EXPECT(nodes[0].prev == NULL);
    TEST_EXPECT(nodes[0].next == &nodes[2]);
    TEST_EXPECT(nodes[2].prev == &nodes[0]);
    TEST_EXPECT(nodes[2].next == NULL);

    List_RemoveNode(&lst, &nodes[0]);
    TEST_EXPECT(lst.front == &nodes[2]);
    TEST_EXPECT(lst.back == &nodes[2]);
    TEST_EXPECT(nodes[2].prev == NULL);
    TEST_EXPECT(nodes[2].next == NULL);

    List_RemoveNode(&lst, &nodes[2]);
    TEST_EXPECT(lst.front == NULL);
    TEST_EXPECT(lst.back == NULL);

    return true;
}

static struct Test const TESTS[] = {
    { .name = "insert front",   .fn = do_insertfront  },
    { .name = "insert back",    .fn = do_insertback   },
    { .name = "insert after",   .fn = do_insertafter  },
    { .name = "insert before",  .fn = do_insertbefore },
    { .name = "remove front",   .fn = do_removefront  },
    { .name = "remove back",    .fn = do_removeback   },
    { .name = "remove node",    .fn = do_removenode   },
};

const struct TestGroup TESTGROUP_LIST = {
    .name = "list",
    .tests = TESTS,
    .testslen = sizeof(TESTS)/sizeof(*TESTS),
};
