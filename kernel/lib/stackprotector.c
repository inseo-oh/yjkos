#include <kernel/lib/noreturn.h>
#include <kernel/panic.h>
#include <stdint.h>

#define STACK_CHECK_GUARD_MAGIC 0xcafebade

uintptr_t __stack_chk_guard __attribute__((used)) = STACK_CHECK_GUARD_MAGIC;

__attribute__((used)) NORETURN void __stack_chk_fail(void) {
    panic("stack smashing detected");
}
