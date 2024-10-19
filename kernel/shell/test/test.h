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

bool test_expect_impl(bool b, char const *expr, char const *func, char const *file, int line);

#define TEST_EXPECT(_x)         if (!test_expect_impl(_x, #_x, __func__, __FILE__, __LINE__)) { return false; }
#define TEST_RUN(_name, _tests) test_run_impl(_name, (_tests), sizeof(_tests)/sizeof(*(_tests)))

// lib
extern const struct testgroup TESTGROUP_BITMAP;
extern const struct testgroup TESTGROUP_BST;
extern const struct testgroup TESTGROUP_LIST;
extern const struct testgroup TESTGROUP_QUEUE;
extern const struct testgroup TESTGROUP_SMATCHER;
extern const struct testgroup TESTGROUP_C_UNISTD;

// mem
extern const struct testgroup TESTGROUP_PMM;
extern const struct testgroup TESTGROUP_HEAP;
