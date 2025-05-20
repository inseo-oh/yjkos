#pragma once
#include <assert.h> /* IWYU pragma: export */

#define STATIC_ASSERT_SIZE(_type, _size) static_assert(sizeof(_type) == (_size), "Size of <" #_type "> is not " #_size " bytes")
#define STATIC_ASSERT_TEST(_b) static_assert((_b), "Test failed")

#define MUST_SUCCEED(_x)   \
    {                      \
        assert(0 <= (_x)); \
        (void)(_x);        \
    }

struct source_location {
    char const *filename;
    char const *function;
    int line;
};

#define SOURCELOCATION_CURRENT() \
    (struct source_location) {    \
        .filename = __FILE__,    \
        .function = __func__,    \
        .line = __LINE__,        \
    }
