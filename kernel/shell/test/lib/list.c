#include "../../shell.h"
#include "../test.h"
#include <kernel/lib/list.h>
#include <string.h>

SHELLFUNC static testresult_t do_insertfront(void) {
    struct list lst;
    struct list_node nodes[3];

    memset(&lst, 0x55, sizeof(lst));
    memset(nodes, 0x55, sizeof(nodes));
    list_init(&lst);

    list_insertfront(&lst, &nodes[0], NULL);
    TEST_EXPECT(lst.front == &nodes[0]);
    TEST_EXPECT(lst.back == &nodes[0]);
    TEST_EXPECT(nodes[0].prev == NULL);
    TEST_EXPECT(nodes[0].next == NULL);

    list_insertfront(&lst, &nodes[1], NULL);
    TEST_EXPECT(lst.front == &nodes[1]);
    TEST_EXPECT(lst.back == &nodes[0]);
    TEST_EXPECT(nodes[1].prev == NULL);
    TEST_EXPECT(nodes[1].next == &nodes[0]);
    TEST_EXPECT(nodes[0].prev == &nodes[1]);
    TEST_EXPECT(nodes[0].next == NULL);

    list_insertfront(&lst, &nodes[2], NULL);
    TEST_EXPECT(lst.front == &nodes[2]);
    TEST_EXPECT(lst.back == &nodes[0]);
    TEST_EXPECT(nodes[2].prev == NULL);
    TEST_EXPECT(nodes[2].next == &nodes[1]);
    TEST_EXPECT(nodes[1].prev == &nodes[2]);
    TEST_EXPECT(nodes[1].next == &nodes[0]);
    TEST_EXPECT(nodes[0].prev == &nodes[1]);
    TEST_EXPECT(nodes[0].next == NULL);

    return TEST_OK;
}

SHELLFUNC static testresult_t do_insertback(void) {
    struct list lst;
    struct list_node nodes[3];

    memset(nodes, 0x55, sizeof(nodes));
    list_init(&lst);

    list_insertback(&lst, &nodes[0], NULL);
    TEST_EXPECT(lst.front == &nodes[0]);
    TEST_EXPECT(lst.back == &nodes[0]);
    TEST_EXPECT(nodes[0].prev == NULL);
    TEST_EXPECT(nodes[0].next == NULL);

    list_insertback(&lst, &nodes[1], NULL);
    TEST_EXPECT(lst.front == &nodes[0]);
    TEST_EXPECT(lst.back == &nodes[1]);
    TEST_EXPECT(nodes[0].prev == NULL);
    TEST_EXPECT(nodes[0].next == &nodes[1]);
    TEST_EXPECT(nodes[1].prev == &nodes[0]);
    TEST_EXPECT(nodes[1].next == NULL);

    list_insertback(&lst, &nodes[2], NULL);
    TEST_EXPECT(lst.front == &nodes[0]);
    TEST_EXPECT(lst.back == &nodes[2]);
    TEST_EXPECT(nodes[0].prev == NULL);
    TEST_EXPECT(nodes[0].next == &nodes[1]);
    TEST_EXPECT(nodes[1].prev == &nodes[0]);
    TEST_EXPECT(nodes[1].next == &nodes[2]);
    TEST_EXPECT(nodes[2].prev == &nodes[1]);
    TEST_EXPECT(nodes[2].next == NULL);

    return TEST_OK;
}

SHELLFUNC static testresult_t do_insertafter(void) {
    struct list lst;
    struct list_node nodes[5];

    memset(nodes, 0x55, sizeof(nodes));
    list_init(&lst);

    list_insertback(&lst, &nodes[0], NULL);
    list_insertback(&lst, &nodes[1], NULL);
    list_insertback(&lst, &nodes[2], NULL);

    list_insertafter(&lst, &nodes[1], &nodes[3], NULL);
    TEST_EXPECT(lst.front == &nodes[0]);
    TEST_EXPECT(lst.back == &nodes[2]);
    TEST_EXPECT(nodes[1].prev == &nodes[0]);
    TEST_EXPECT(nodes[1].next == &nodes[3]);
    TEST_EXPECT(nodes[3].prev == &nodes[1]);
    TEST_EXPECT(nodes[3].next == &nodes[2]);
    TEST_EXPECT(nodes[2].prev == &nodes[3]);
    TEST_EXPECT(nodes[2].next == NULL);

    list_insertafter(&lst, &nodes[2], &nodes[4], NULL);
    TEST_EXPECT(lst.front == &nodes[0]);
    TEST_EXPECT(lst.back == &nodes[4]);
    TEST_EXPECT(nodes[2].prev == &nodes[3]);
    TEST_EXPECT(nodes[2].next == &nodes[4]);
    TEST_EXPECT(nodes[4].prev == &nodes[2]);
    TEST_EXPECT(nodes[4].next == NULL);

    return TEST_OK;
}

SHELLFUNC static testresult_t do_insertbefore(void) {
    struct list lst;
    struct list_node nodes[5];

    memset(nodes, 0x55, sizeof(nodes));
    list_init(&lst);

    list_insertback(&lst, &nodes[0], NULL);
    list_insertback(&lst, &nodes[1], NULL);
    list_insertback(&lst, &nodes[2], NULL);

    list_insertbefore(&lst, &nodes[1], &nodes[3], NULL);
    TEST_EXPECT(lst.front == &nodes[0]);
    TEST_EXPECT(lst.back == &nodes[2]);
    TEST_EXPECT(nodes[0].prev == NULL);
    TEST_EXPECT(nodes[0].next == &nodes[3]);
    TEST_EXPECT(nodes[3].prev == &nodes[0]);
    TEST_EXPECT(nodes[3].next == &nodes[1]);
    TEST_EXPECT(nodes[1].prev == &nodes[3]);
    TEST_EXPECT(nodes[1].next == &nodes[2]);

    list_insertbefore(&lst, &nodes[0], &nodes[4], NULL);
    TEST_EXPECT(lst.front == &nodes[4]);
    TEST_EXPECT(lst.back == &nodes[2]);
    TEST_EXPECT(nodes[4].prev == NULL);
    TEST_EXPECT(nodes[4].next == &nodes[0]);
    TEST_EXPECT(nodes[0].prev == &nodes[4]);
    TEST_EXPECT(nodes[0].next == &nodes[3]);

    return TEST_OK;
}

SHELLFUNC static testresult_t do_removefront(void) {
    struct list lst;
    struct list_node nodes[3];
    struct list_node *removednode;

    memset(nodes, 0x55, sizeof(nodes));
    list_init(&lst);

    list_insertback(&lst, &nodes[0], NULL);
    list_insertback(&lst, &nodes[1], NULL);
    list_insertback(&lst, &nodes[2], NULL);
    
    removednode = list_removefront(&lst);
    TEST_EXPECT(removednode == &nodes[0]);
    TEST_EXPECT(lst.front == &nodes[1]);
    TEST_EXPECT(lst.back == &nodes[2]);
    TEST_EXPECT(nodes[1].prev == NULL);
    TEST_EXPECT(nodes[1].next == &nodes[2]);
    TEST_EXPECT(nodes[2].prev == &nodes[1]);
    TEST_EXPECT(nodes[2].next == NULL);

    removednode = list_removefront(&lst);
    TEST_EXPECT(removednode == &nodes[1]);
    TEST_EXPECT(lst.front == &nodes[2]);
    TEST_EXPECT(lst.back == &nodes[2]);
    TEST_EXPECT(nodes[2].prev == NULL);
    TEST_EXPECT(nodes[2].next == NULL);

    removednode = list_removefront(&lst);
    TEST_EXPECT(removednode == &nodes[2]);
    TEST_EXPECT(lst.front == NULL);
    TEST_EXPECT(lst.back == NULL);

    removednode = list_removefront(&lst);
    TEST_EXPECT(removednode == NULL);
    return TEST_OK;
}

SHELLFUNC static testresult_t do_removeback(void) {
    struct list lst;
    struct list_node nodes[3];
    struct list_node *removednode;

    memset(nodes, 0x55, sizeof(nodes));
    list_init(&lst);

    list_insertback(&lst, &nodes[0], NULL);
    list_insertback(&lst, &nodes[1], NULL);
    list_insertback(&lst, &nodes[2], NULL);

    removednode = list_removeback(&lst);
    TEST_EXPECT(removednode == &nodes[2]);
    TEST_EXPECT(lst.front == &nodes[0]);
    TEST_EXPECT(lst.back == &nodes[1]);
    TEST_EXPECT(nodes[0].prev == NULL);
    TEST_EXPECT(nodes[0].next == &nodes[1]);
    TEST_EXPECT(nodes[1].prev == &nodes[0]);
    TEST_EXPECT(nodes[1].next == NULL);

    removednode = list_removeback(&lst);
    TEST_EXPECT(removednode == &nodes[1]);
    TEST_EXPECT(lst.front == &nodes[0]);
    TEST_EXPECT(lst.back == &nodes[0]);
    TEST_EXPECT(nodes[0].prev == NULL);
    TEST_EXPECT(nodes[0].next == NULL);

    removednode = list_removeback(&lst);
    TEST_EXPECT(removednode == &nodes[0]);
    TEST_EXPECT(lst.front == NULL);
    TEST_EXPECT(lst.back == NULL);

    removednode = list_removeback(&lst);
    TEST_EXPECT(removednode == NULL);
    return TEST_OK;
}

SHELLFUNC static testresult_t do_removenode(void) {
    struct list lst;
    struct list_node nodes[3];

    memset(nodes, 0x55, sizeof(nodes));
    list_init(&lst);

    list_insertback(&lst, &nodes[0], NULL);
    list_insertback(&lst, &nodes[1], NULL);
    list_insertback(&lst, &nodes[2], NULL);
    
    list_removenode(&lst, &nodes[1]);
    TEST_EXPECT(lst.front == &nodes[0]);
    TEST_EXPECT(lst.back == &nodes[2]);
    TEST_EXPECT(nodes[0].prev == NULL);
    TEST_EXPECT(nodes[0].next == &nodes[2]);
    TEST_EXPECT(nodes[2].prev == &nodes[0]);
    TEST_EXPECT(nodes[2].next == NULL);

    list_removenode(&lst, &nodes[0]);
    TEST_EXPECT(lst.front == &nodes[2]);
    TEST_EXPECT(lst.back == &nodes[2]);
    TEST_EXPECT(nodes[2].prev == NULL);
    TEST_EXPECT(nodes[2].next == NULL);

    list_removenode(&lst, &nodes[2]);
    TEST_EXPECT(lst.front == NULL);
    TEST_EXPECT(lst.back == NULL);

    return TEST_OK;
}

SHELLRODATA static struct test const TESTS[] = {
    { .name = "insert front",   .fn = do_insertfront  },
    { .name = "insert back",    .fn = do_insertback   },
    { .name = "insert after",   .fn = do_insertafter  },
    { .name = "insert before",  .fn = do_insertbefore },
    { .name = "remove front",   .fn = do_removefront  },
    { .name = "remove back",    .fn = do_removeback   },
    { .name = "remove node",    .fn = do_removenode   },
};

SHELLDATA const struct testgroup TESTGROUP_LIST = {
    .name = "list",
    .tests = TESTS,
    .testslen = sizeof(TESTS)/sizeof(*TESTS),
};
