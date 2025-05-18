#include "shell.h"
#include <kernel/io/co.h>

static int program_main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    Co_Printf("Hello, world!\n");
    return 0;
}

struct Shell_Program g_shell_program_hello = {
    .name = "hello",
    .main = program_main,
};

