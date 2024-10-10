#pragma once
#include <kernel/lib/list.h>
#include <kernel/status.h>

// Shell is located in its own section, so that it's possible to reclaim
// memory used by kernel shell in the future.
#define SHELLSECTION(_section)    __attribute__((section(_section ".shell")))
#define SHELLFUNC                 SHELLSECTION(".text")
#define SHELLDATA                 SHELLSECTION(".data")
#define SHELLRODATA               SHELLSECTION(".rodata")
#define SHELLBSS                  SHELLSECTION(".bss")

typedef struct shell_program shell_program_t;
struct shell_program {
    char const *name;
    int (*main)(int argc, char **argv);
    list_node_t node;
};

FAILABLE_FUNCTION shell_execcmd(char const *str) SHELLFUNC;
void shell_repl(void) SHELLFUNC;
void shell_init(void) SHELLFUNC;

// Programs

extern shell_program_t g_shell_program_runtest SHELLDATA;
extern shell_program_t g_shell_program_hello SHELLDATA;
extern shell_program_t g_shell_program_kdoom SHELLDATA;
extern shell_program_t g_shell_program_rawvidplay SHELLDATA;
