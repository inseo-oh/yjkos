#include <kernel/arch/hcf.h>
#include <kernel/arch/interrupts.h>
#include <kernel/arch/stacktrace.h>
#include <kernel/io/co.h>
#include <kernel/panic.h>
#include <stddef.h>

[[noreturn]] void Panic(char const *msg) {
    Arch_Irq_Disable();
    Co_PutString("\nFATAL SOFTWARE FAILURE -- SYSTEM NEEDS TO RESTART.\n");
    if (msg != NULL) {
        Co_PutString(msg);
        Co_PutChar('\n');
    }
    Arch_Stacktrace();
    Arch_Hcf();
}
