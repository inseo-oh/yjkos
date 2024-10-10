#include <kernel/arch/hcf.h>
#include <kernel/io/tty.h>
#include <kernel/lib/noreturn.h>
#include <kernel/panic.h>
#include <stddef.h>

NORETURN void panic(char const *msg) {
    tty_puts("\nFATAL SOFTWARE FAILURE -- SYSTEM NEEDS TO RESTART.\n");
    if (msg != NULL) {
        tty_puts(msg);
        tty_putc('\n');
    }
    arch_hcf();
}
