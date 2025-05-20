#include <assert.h>
#include <kernel/io/co.h>
#include <kernel/panic.h>
#include <stddef.h>

void __assert_fail(const char *assertion, const char *file, unsigned int line, const char *function) {
    co_printf("assertion failed at %s(%s:%d): %s\n", function, file, line, assertion);
    panic(NULL);
}
