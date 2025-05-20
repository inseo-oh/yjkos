#include "test.h"
#include <kernel/io/co.h>

bool __test_expect(bool b, char const *expr, char const *func, char const *file, int line) {
    if (!b) {
        co_printf("test failed in %s(%s:%d) - failed test expresion: %s\n", func, file, line, expr);
    }
    return b;
}
