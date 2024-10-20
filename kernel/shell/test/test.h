#pragma once
#include <kernel/io/tty.h>
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

bool test_expect_impl(
    bool b, char const *expr, char const *func, char const *file, int line);

#define TEST_EXPECT(_x)                                             \
    if (!test_expect_impl(_x, #_x, __func__, __FILE__, __LINE__)) { \
        return false;                                               \
    }
#define TEST_RUN(_name, _tests) \
    test_run_impl(_name, (_tests), sizeof(_tests)/sizeof(*(_tests)))

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

#define X(_x)   extern const struct testgroup _x;
ENUMERATE_TESTGROUPS(X)
#undef X
