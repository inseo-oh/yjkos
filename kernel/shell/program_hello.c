#include "shell.h"
#include <kernel/io/tty.h>

static int program_main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    tty_printf("Hello, world!\n");
    return 0;
}

struct shell_program g_shell_program_hello = {
    .name = "hello",
    .main = program_main,
};

