#include "shell.h"

static int program_main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    return 1;
}

struct shell_program g_shell_program_false = {
    .name = "false",
    .main = program_main,
};

