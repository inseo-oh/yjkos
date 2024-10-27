#pragma once
#include <assert.h> // IWYU pragma: export

#ifdef __GNUC__
#define WARN_UNUSED_RESULT      __attribute__((warn_unused_result))
#else
#define WARN_UNUSED_RESULT
#endif

#define STATIC_ASSERT_SIZE(_type, _size) \
    static_assert(sizeof(_type) == _size,\
        "Size of <" #_type "> is not " #_size " bytes")
#define STATIC_ASSERT_TEST(_b)             static_assert((_b), "Test failed")

#define MUST_SUCCEED(_x) {  \
    assert(0 <= (_x));      \
    (void)(_x);             \
}

struct sourcelocation {
    char const *filename;
    char const *function;
    int line;
};

#define SOURCELOCATION_CURRENT() (struct sourcelocation){\
    .filename = __FILE__,\
    .function = __func__,\
    .line     = __LINE__,\
}
