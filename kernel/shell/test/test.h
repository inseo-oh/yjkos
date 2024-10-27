#pragma once
#include <kernel/io/co.h>
#include <stdbool.h>

struct test {
    char const *name;
    bool (*fn)(void);
};

struct testgroup {
    char const *name;
    struct test const *tests;
    size_t testslen;
};

bool __test_expect(
    bool b, char const *expr, char const *func, char const *file, int line);

#define TEST_EXPECT(_x)                                          \
    if (!__test_expect(_x, #_x, __func__, __FILE__, __LINE__)) { \
        return false;                                            \
    }

#define ENUMERATE_TESTGROUPS(_x)    \
    /* lib */                       \
    _x(TESTGROUP_BITMAP)            \
    _x(TESTGROUP_BST)               \
    _x(TESTGROUP_LIST)              \
    _x(TESTGROUP_QUEUE)             \
    _x(TESTGROUP_SMATCHER)          \
    _x(TESTGROUP_C_UNISTD)          \
    /* mem */                       \
    _x(TESTGROUP_PMM)               \
    _x(TESTGROUP_HEAP)              \
    /* tasks */                     \
    _x(TESTGROUP_MUTEX)             \

#define X(_x)   extern const struct testgroup _x;
ENUMERATE_TESTGROUPS(X)
#undef X
