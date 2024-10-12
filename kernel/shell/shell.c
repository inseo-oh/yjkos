#include "shell.h"
#include <assert.h>
#include <kernel/io/tty.h>
#include <kernel/lib/list.h>
#include <kernel/lib/smatcher.h>
#include <kernel/mem/heap.h>
#include <kernel/panic.h>
#include <kernel/status.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>


//------------------------------- Configuration -------------------------------

// Dump the command parse result?
static bool const CONFIG_DUMPCMD = false; 

//-----------------------------------------------------------------------------


enum cmdkind {
    CMDKIND_EMPTY,
    CMDKIND_RUNPROGRAM,
};

enum {
    SHELL_MAX_CMDLINE_LEN = 80,
    SHELL_MAX_NAME_LEN    = 20,
};

union shellcmd {
    enum cmdkind kind;
    struct {
        enum cmdkind kind;
        char **argv;
        int argc;
    } runprogram;
};

SHELLBSS static struct list s_programs;

SHELLFUNC static FAILABLE_FUNCTION parsecmd(union shellcmd *out, struct smatcher *cmdstr) {
FAILABLE_PROLOGUE
    size_t oldcurrentindex = cmdstr->currentindex;
    char **argv = NULL;
    int argc = 0;
    smatcher_skipwhitespaces(cmdstr);
    if (cmdstr->currentindex == cmdstr->len) {
        out->kind = CMDKIND_EMPTY;
    } else {
        while(1) {
            smatcher_skipwhitespaces(cmdstr);
            if (cmdstr->currentindex == cmdstr->len || (smatcher_consumestringifmatch(cmdstr, ";"))) {
                break;
            }
            char const *str;
            size_t len;
            bool matchok = smatcher_consumeword(&str, &len, cmdstr);
            (void)matchok;
            assert(matchok);
            if (argc == INT_MAX) {
                THROW(ERR_NOMEM);
            }
            size_t newargc = argc + 1;
            if ((SIZE_MAX / sizeof(void *)) < (size_t)argc) {
                THROW(ERR_NOMEM);
            }
            size_t newargvsize = newargc * sizeof(void *);
            argv = heap_realloc(argv, newargvsize, 0);
            if (argv == NULL) {
                THROW(ERR_NOMEM);
            }
            argv[newargc - 1] = heap_alloc(len + 1, 0);
            if (argv[newargc - 1] == NULL) {
                THROW(ERR_NOMEM);
            }
            memcpy(argv[newargc - 1], str, len);
            argv[newargc - 1][len] = '\0';
            argc = newargc;
        }
        out->runprogram.kind = CMDKIND_RUNPROGRAM;
        out->runprogram.argc = argc;
        out->runprogram.argv = argv;
    }
FAILABLE_EPILOGUE_BEGIN
    if (DID_FAIL) {
        for (int i = 0; i < argc; i++) {
            heap_free(argv[i]);
        }
        heap_free(argv);
        cmdstr->currentindex = oldcurrentindex;
    }
FAILABLE_EPILOGUE_END
}

SHELLFUNC static void cmd_destroy(union shellcmd *cmd) {
    switch(cmd->kind) {
        case CMDKIND_RUNPROGRAM:
            for (int i = 0; i < cmd->runprogram.argc; i++) {
                heap_free(cmd->runprogram.argv[i]);
            }
            heap_free(cmd->runprogram.argv);
        case CMDKIND_EMPTY:
            break;
    }
}

SHELLFUNC static void cmd_dump(union shellcmd const *cmd) {
    switch(cmd->kind) {
        case CMDKIND_RUNPROGRAM:
            tty_printf("[cmd_dump] RUNPROGRAM\n");
            tty_printf("[cmd_dump]  - argc %d\n", cmd->runprogram.argc);
            for (int i = 0; i < cmd->runprogram.argc; i++) {
                tty_printf("[cmd_dump]  - argv[%d] - [%s]\n", i, cmd->runprogram.argv[i]);
            }
            break;
        case CMDKIND_EMPTY:
            tty_printf("[cmd_dump] EMPTY\n");
            break;
        default:
            tty_printf("[cmd_dump] UNKNOWN CMD\n");
    }
}

SHELLFUNC static int cmd_exec(union shellcmd const *cmd) {
    switch(cmd->kind) {
        case CMDKIND_RUNPROGRAM: {
            assert(cmd->runprogram.argc != 0);
            assert(cmd->runprogram.argv != NULL);
            struct shell_program *program_to_run = NULL;
            for (struct list_node * programnode = s_programs.front; programnode != NULL; programnode = programnode->next) {
                struct shell_program *program = programnode->data;
                if (strcmp(program->name, cmd->runprogram.argv[0]) == 0) {
                    program_to_run = program;
                    break;
                }
            }
            if (program_to_run == NULL) {
                tty_printf("%s: command not found\n", cmd->runprogram.argv[0]);
                return 127;
            }
            return program_to_run->main(cmd->runprogram.argc, cmd->runprogram.argv);
        }
        case CMDKIND_EMPTY:
            return 0;
        default:
            panic("shell: cmd_exec internal error - unknown cmd type");
    }
}

SHELLFUNC static void registerprogram(struct shell_program *program) {
    list_insertback(&s_programs, &program->node, program);
}

SHELLFUNC FAILABLE_FUNCTION shell_execcmd(char const *str) {
FAILABLE_PROLOGUE
    union shellcmd cmd;
    struct smatcher linematcher;
    smatcher_init(&linematcher, str);
    TRY(parsecmd(&cmd, &linematcher));
    if (cmd.kind != CMDKIND_EMPTY) {
        if (CONFIG_DUMPCMD) {
            cmd_dump(&cmd);
        }
        int e = cmd_exec(&cmd);
        if (e != 0) {
            THROW(ERR_SUBCMDDIED);
        }
        cmd_destroy(&cmd);
    }
FAILABLE_EPILOGUE_BEGIN
FAILABLE_EPILOGUE_END
}

SHELLFUNC void shell_repl(void) {
    char cmdline[SHELL_MAX_CMDLINE_LEN + 1];
    status_t status;

    while(1) {
        size_t cursorpos = 0;
        cmdline[0] = '\0';
        tty_printf("kernel> ");

        while(1) {
            char c = tty_getchar();
            if (c == CON_BACKSPACE || c == CON_DELETE) {
                if (cursorpos != 0) {
                    cursorpos--;
                    tty_printf("\b");
                }
            } else if ((c == '\r') || (c == '\n')) {
                cmdline[cursorpos] = '\0';
                tty_printf("\n");
                break;
            } else {
                if (cursorpos < (SHELL_MAX_CMDLINE_LEN - 1)) {
                    cmdline[cursorpos] = c;
                    tty_printf("%c", c);
                    cursorpos++;
                }
            }
        }

        status = shell_execcmd(cmdline);
        if (status != OK) {
            tty_printf("command error %d\n", status);
        }
    }
}

SHELLFUNC void shell_init(void) {
    registerprogram(&g_shell_program_runtest);
    registerprogram(&g_shell_program_hello);
    registerprogram(&g_shell_program_kdoom);
    registerprogram(&g_shell_program_rawvidplay);
}

