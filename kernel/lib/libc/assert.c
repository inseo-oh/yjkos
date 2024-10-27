#include <assert.h>
#include <kernel/io/tty.h>
#include <kernel/panic.h>
#include <stddef.h>

void __assert_fail(const char *assertion, const char *file, unsigned int line, const char *function) {
    tty_printf("assertion failed at %s(%s:%d): %s\n", function, file, line, assertion);
    panic(NULL);
}
