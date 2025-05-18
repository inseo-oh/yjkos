#include "test.h"
#include <kernel/io/co.h>

bool __TestExpect(bool b, char const *expr, char const *func, char const *file, int line) {
    if (!b) {
        Co_Printf("test failed in %s(%s:%d) - failed test expresion: %s\n", func, file, line, expr);
    }
    return b;
}
