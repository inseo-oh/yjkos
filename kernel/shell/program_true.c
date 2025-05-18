#include "shell.h"

static int program_main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    return 0;
}

struct Shell_Program g_shell_program_true = {
    .name = "true",
    .main = program_main,
};

