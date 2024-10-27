#include <kernel/arch/hcf.h>
#include <kernel/arch/interrupts.h>
#include <kernel/arch/stacktrace.h>
#include <kernel/io/co.h>
#include <kernel/lib/noreturn.h>
#include <kernel/panic.h>
#include <stddef.h>

NORETURN void panic(char const *msg) {
    arch_interrupts_disable();
    co_puts("\nFATAL SOFTWARE FAILURE -- SYSTEM NEEDS TO RESTART.\n");
    if (msg != NULL) {
        co_puts(msg);
        co_putc('\n');
    }
    arch_stacktrace();
    arch_hcf();
}
