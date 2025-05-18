#pragma once
#include <kernel/lib/list.h>

struct Shell_Program {
    char const *name;
    int (*main)(int argc, char **argv);
    struct List_Node node;
};

static const int SHELL_EXITCODE_OUTOFMEMORY = -1;
static const int SHELL_EXITCODE_OK = 0;
static const int SHELL_EXITCODE_BUILTINMUISUSE = 2;
static const int SHELL_EXITCODE_NOTEXECUTABLE = 126;
static const int SHELL_EXITCODE_NOCOMMAND = 127;

int Shell_ExecCmd(char const *str);
void Shell_Repl(void);
void Shell_Init(void);

/* Programs *******************************************************************/

/* clang-format off */
#define ENUMERATE_SHELLPROGRAMS(_x) \
    _x(g_shell_program_runtest)     \
    _x(g_shell_program_hello)       \
    _x(g_shell_program_kdoom)       \
    _x(g_shell_program_rawvidplay)  \
    _x(g_shell_program_ls)          \
    _x(g_shell_program_true)        \
    _x(g_shell_program_false)       \
    _x(g_shell_program_cat)         \
    _x(g_shell_program_uname)       \

#define X(_x)   extern struct Shell_Program _x;
ENUMERATE_SHELLPROGRAMS(X)
#undef X

/* clang-format on */
