#include <kernel/arch/hcf.h>
#include <kernel/arch/interrupts.h>
#include <kernel/arch/stacktrace.h>
#include <kernel/io/co.h>
#include <kernel/panic.h>

[[noreturn]] void panic(char const *msg) {
    arch_irq_disable();
    co_put_string("\nFATAL SOFTWARE FAILURE -- SYSTEM NEEDS TO RESTART.\n");
    if (msg != nullptr) {
        co_put_string(msg);
        co_put_char('\n');
    }
    arch_stacktrace();
    arch_hcf();
}
