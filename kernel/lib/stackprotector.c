#include <kernel/panic.h>
#include <stdint.h>

#define STACK_CHECK_GUARD_MAGIC 0x7afebade

[[gnu::used]] uintptr_t __stack_chk_guard = STACK_CHECK_GUARD_MAGIC;

[[noreturn, gnu::used]] void __stack_chk_fail(void) {
    Panic("stack smashing detected");
}
