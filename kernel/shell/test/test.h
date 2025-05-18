#pragma once
#include <kernel/io/co.h>
#include <stddef.h>

struct Test {
    char const *name;
    bool (*fn)(void);
};

struct TestGroup {
    char const *name;
    struct Test const *tests;
    size_t testslen;
};

bool __TestExpect(bool b, char const *expr, char const *func, char const *file, int line);

#define TEST_EXPECT(_x)                                         \
    if (!__TestExpect(_x, #_x, __func__, __FILE__, __LINE__)) { \
        return false;                                           \
    }

/* clang-format off */
#define ENUMERATE_TESTGROUPS(_x)    \
    /* lib */                       \
    _x(TESTGROUP_BITMAP)            \
    _x(TESTGROUP_BST)               \
    _x(TESTGROUP_C_UNISTD)          \
    _x(TESTGROUP_LIST)              \
    _x(TESTGROUP_PATHREADER)        \
    _x(TESTGROUP_QUEUE)             \
    _x(TESTGROUP_SMATCHER)          \
    /* mem */                       \
    _x(TESTGROUP_PMM)               \
    _x(TESTGROUP_HEAP)              \
    /* tasks */                     \
    _x(TESTGROUP_MUTEX)

/* clang-format on */

#define X(_x) extern const struct TestGroup _x;
ENUMERATE_TESTGROUPS(X)
#undef X
