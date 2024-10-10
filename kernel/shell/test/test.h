#pragma once
#include <kernel/io/tty.h>
#include <stdbool.h>

typedef enum {
    TEST_OK,
    TEST_FAIL,
} testresult_t;

typedef struct test test_t;
struct test {
    char const *name;
    testresult_t (*fn)(void);
};

typedef struct testgroup testgroup_t;
struct testgroup {
    char const *name;
    test_t const *tests;
    size_t testslen;
};

bool test_expect_impl(bool b, char const *expr, char const *func, char const *file, int line);

#define TEST_EXPECT(_x)         if (!test_expect_impl(_x, #_x, __func__, __FILE__, __LINE__)) { return TEST_FAIL; }
#define TEST_RUN(_name, _tests) test_run_impl(_name, (_tests), sizeof(_tests)/sizeof(*(_tests)))

// lib
extern const testgroup_t TESTGROUP_BITMAP;
extern const testgroup_t TESTGROUP_BST;
extern const testgroup_t TESTGROUP_LIST;
extern const testgroup_t TESTGROUP_QUEUE;
extern const testgroup_t TESTGROUP_SMATCHER;

// mem
extern const testgroup_t TESTGROUP_PMM;
extern const testgroup_t TESTGROUP_HEAP;
