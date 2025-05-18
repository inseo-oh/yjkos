#include "shell.h"
#include <assert.h>
#include <kernel/io/co.h>
#include <kernel/lib/diagnostics.h>
#include <kernel/lib/list.h>
#include <kernel/lib/smatcher.h>
#include <kernel/mem/heap.h>
#include <kernel/panic.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

/******************************** Configuration *******************************/

/*
 * Dump the command parse result?
 */
static bool const CONFIG_DUMPCMD = false;

/******************************************************************************/

typedef enum {
    CMDKIND_EMPTY,
    CMDKIND_RUNPROGRAM,
} CMDKIND;

#define SHELL_MAX_CMDLINE_LEN 80
#define SHELL_MAX_NAME_LEN 20

union shellcmd {
    CMDKIND kind;
    struct {
        CMDKIND kind;
        char **argv;
        int argc;
    } runprogram;
};

static struct List s_programs;

static int parse_cmd_runprogram(union shellcmd *out, struct SMatcher *cmdstr) {
    size_t old_current_index = cmdstr->currentindex;
    int ret = SHELL_EXITCODE_OK;
    char **argv = NULL;
    int argc = 0;
    while (1) {
        Smatcher_SkipWhitespaces(cmdstr);
        if ((cmdstr->currentindex == cmdstr->len) || (Smatcher_ConsumeStrIfMatch(cmdstr, ";"))) {
            break;
        }
        char const *str = NULL;
        size_t len = 0;
        bool matchok = Smatcher_ConsumeWord(&str, &len, cmdstr);
        (void)matchok;
        assert(matchok);
        if (argc == INT_MAX) {
            goto fail_alloc;
        }
        int newargc = argc + 1;
        if ((SIZE_MAX / sizeof(void *)) < (size_t)argc) {
            goto fail_alloc;
        }
        size_t newargvsize = newargc * sizeof(void *);
        argv = Heap_Realloc(argv, newargvsize, 0);
        if (argv == NULL) {
            goto fail_alloc;
        }
        argv[newargc - 1] = Heap_Alloc(len + 1, 0);
        if (argv[newargc - 1] == NULL) {
            goto fail_alloc;
        }
        memcpy(argv[newargc - 1], str, len);
        argv[newargc - 1][len] = '\0';
        argc = newargc;
    }
    out->runprogram.kind = CMDKIND_RUNPROGRAM;
    out->runprogram.argc = argc;
    out->runprogram.argv = argv;
    goto out;
fail_alloc:
    if (argv != NULL) {
        for (int i = 0; i < argc; i++) {
            Heap_Free(argv[i]);
        }
        Heap_Free(argv);
    }
    cmdstr->currentindex = old_current_index;
out:
    return ret;
}

/*
 * Returns shell exit code (See SHELL_EXITCODE_~)
 */
[[nodiscard]] static int parse_cmd(union shellcmd *out, struct SMatcher *cmdstr) {
    int result = SHELL_EXITCODE_OK;
    memset(out, 0, sizeof(*out));
    Smatcher_SkipWhitespaces(cmdstr);
    if (cmdstr->currentindex == cmdstr->len) {
        out->kind = CMDKIND_EMPTY;
    } else {
        result = parse_cmd_runprogram(out, cmdstr);
    }
    goto out;
out:
    return result;
}

static void cmd_destroy(union shellcmd *cmd) {
    switch (cmd->kind) {
    case CMDKIND_RUNPROGRAM:
        for (int i = 0; i < cmd->runprogram.argc; i++) {
            Heap_Free(cmd->runprogram.argv[i]);
        }
        Heap_Free(cmd->runprogram.argv);
    case CMDKIND_EMPTY:
        break;
    }
}

static void cmd_dump(union shellcmd const *cmd) {
    switch (cmd->kind) {
    case CMDKIND_RUNPROGRAM:
        Co_Printf("[cmd_dump] RUNPROGRAM\n");
        Co_Printf("[cmd_dump]  - argc %d\n", cmd->runprogram.argc);
        for (int i = 0; i < cmd->runprogram.argc; i++) {
            Co_Printf("[cmd_dump]  - argv[%d] - [%s]\n", i, cmd->runprogram.argv[i]);
        }
        break;
    case CMDKIND_EMPTY:
        Co_Printf("[cmd_dump] EMPTY\n");
        break;
    default:
        Co_Printf("[cmd_dump] UNKNOWN CMD\n");
    }
}

static int cmd_exec(union shellcmd const *cmd) {
    switch (cmd->kind) {
    case CMDKIND_RUNPROGRAM: {
        assert(cmd->runprogram.argc != 0);
        assert(cmd->runprogram.argv != NULL);
        struct Shell_Program *program_to_run = NULL;
        LIST_FOREACH(&s_programs, programnode) {
            struct Shell_Program *program = programnode->data;
            if (strcmp(program->name, cmd->runprogram.argv[0]) == 0) {
                program_to_run = program;
                break;
            }
        }
        if (program_to_run == NULL) {
            Co_Printf("%s: command not found\n", cmd->runprogram.argv[0]);
            return 127;
        }
        optind = 1;
        opterr = 1;
        return program_to_run->main(cmd->runprogram.argc, cmd->runprogram.argv);
    }
    case CMDKIND_EMPTY:
        return 0;
    default:
        Panic("shell: cmd_exec internal error - unknown cmd type");
    }
}

static void registerprogram(struct Shell_Program *program) {
    List_InsertBack(&s_programs, &program->node, program);
}

int Shell_ExecCmd(char const *str) {
    int ret = 0;
    union shellcmd cmd;
    struct SMatcher linematcher;
    Smatcher_Init(&linematcher, str);
    ret = parse_cmd(&cmd, &linematcher);
    if (ret < 0) {
        return ret;
    }
    if (cmd.kind != CMDKIND_EMPTY) {
        if (CONFIG_DUMPCMD) {
            cmd_dump(&cmd);
        }
        ret = cmd_exec(&cmd);
        cmd_destroy(&cmd);
        if (ret != 0) {
            return ret;
        }
    }
    return 0;
}

void Shell_Repl(void) {
    char cmdline[SHELL_MAX_CMDLINE_LEN + 1];

    while (1) {
        size_t cursorpos = 0;
        cmdline[0] = '\0';
        Co_Printf("kernel> ");

        while (1) {
            int c = Co_GetChar();
            if (c == CON_BACKSPACE || c == CON_DELETE) {
                if (cursorpos != 0) {
                    cursorpos--;
                    Co_Printf("\b");
                }
            } else if ((c == '\r') || (c == '\n')) {
                cmdline[cursorpos] = '\0';
                Co_Printf("\n");
                break;
            } else {
                if (cursorpos < (SHELL_MAX_CMDLINE_LEN - 1)) {
                    cmdline[cursorpos] = (char)c;
                    Co_Printf("%c", c);
                    cursorpos++;
                }
            }
        }

        int ret = Shell_ExecCmd(cmdline);
        if (ret != 0) {
            Co_Printf("command error %d\n", ret);
        }
    }
}

void Shell_Init(void) {
#define X(_x) registerprogram(&(_x));
    ENUMERATE_SHELLPROGRAMS(X)
#undef X
}
