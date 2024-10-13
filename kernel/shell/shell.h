#pragma once
#include <kernel/lib/list.h>
#include <stdbool.h>

// Shell is located in its own section, so that it's possible to reclaim
// memory used by kernel shell in the future.
#define SHELLSECTION(_section)    __attribute__((section(_section ".shell")))
#define SHELLFUNC                 SHELLSECTION(".text")
#define SHELLDATA                 SHELLSECTION(".data")
#define SHELLRODATA               SHELLSECTION(".rodata")
#define SHELLBSS                  SHELLSECTION(".bss")

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

int shell_execcmd(char const *str) SHELLFUNC;
void shell_repl(void) SHELLFUNC;
void shell_init(void) SHELLFUNC;

// Programs

extern struct shell_program g_shell_program_runtest SHELLDATA;
extern struct shell_program g_shell_program_hello SHELLDATA;
extern struct shell_program g_shell_program_kdoom SHELLDATA;
extern struct shell_program g_shell_program_rawvidplay SHELLDATA;
