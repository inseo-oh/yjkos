#pragma once
#include <kernel/lib/list.h>
#include <stdbool.h>

struct shell_program {
    char const *name;
    int (*main)(int argc, char **argv);
    struct list_node node;
};

static const int SHELL_EXITCODE_OUTOFMEMORY    = -1;
static const int SHELL_EXITCODE_OK             = 0;
static const int SHELL_EXITCODE_BUILTINMUISUSE = 2;
static const int SHELL_EXITCODE_NOTEXECUTABLE  = 126;
static const int SHELL_EXITCODE_NOCOMMAND      = 127;

int shell_execcmd(char const *str);
void shell_repl(void);
void shell_init(void);

// Programs

extern struct shell_program g_shell_program_runtest;
extern struct shell_program g_shell_program_hello;
extern struct shell_program g_shell_program_kdoom;
extern struct shell_program g_shell_program_rawvidplay;
extern struct shell_program g_shell_program_ls;
extern struct shell_program g_shell_program_true;
extern struct shell_program g_shell_program_false;
extern struct shell_program g_shell_program_cat;
