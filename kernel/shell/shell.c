#include "shell.h"
#include <assert.h>
#include <kernel/io/co.h>
#include <kernel/lib/diagnostics.h>
#include <kernel/lib/list.h>
#include <kernel/lib/strutil.h>
#include <kernel/mem/heap.h>
#include <kernel/panic.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
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

static struct list s_programs;

static int parse_cmd_runprogram(union shellcmd *out, struct smatcher *cmdstr) {
    size_t old_current_index = cmdstr->currentindex;
    int ret = SHELL_EXITCODE_OK;
    char **argv = nullptr;
    int argc = 0;
    while (1) {
        smatcher_skip_whitespaces(cmdstr);
        if ((cmdstr->currentindex == cmdstr->len) || (smatcher_consume_str_if_match(cmdstr, ";"))) {
            break;
        }
        char const *str = nullptr;
        size_t len = 0;
        bool matchok = smatcher_consume_word(&str, &len, cmdstr);
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
        argv = heap_realloc(argv, newargvsize, 0);
        if (argv == nullptr) {
            goto fail_alloc;
        }
        argv[newargc - 1] = heap_alloc(len + 1, 0);
        if (argv[newargc - 1] == nullptr) {
            goto fail_alloc;
        }
        vmemcpy(argv[newargc - 1], str, len);
        argv[newargc - 1][len] = '\0';
        argc = newargc;
    }
    out->runprogram.kind = CMDKIND_RUNPROGRAM;
    out->runprogram.argc = argc;
    out->runprogram.argv = argv;
    goto out;
fail_alloc:
    if (argv != nullptr) {
        for (int i = 0; i < argc; i++) {
            heap_free(argv[i]);
        }
        heap_free(argv);
    }
    cmdstr->currentindex = old_current_index;
out:
    return ret;
}

/*
 * Returns shell exit code (See SHELL_EXITCODE_~)
 */
[[nodiscard]] static int parse_cmd(union shellcmd *out, struct smatcher *cmdstr) {
    int result = SHELL_EXITCODE_OK;
    vmemset(out, 0, sizeof(*out));
    smatcher_skip_whitespaces(cmdstr);
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
            heap_free(cmd->runprogram.argv[i]);
        }
        heap_free(cmd->runprogram.argv);
    case CMDKIND_EMPTY:
        break;
    }
}

static void cmd_dump(union shellcmd const *cmd) {
    switch (cmd->kind) {
    case CMDKIND_RUNPROGRAM:
        co_printf("[cmd_dump] RUNPROGRAM\n");
        co_printf("[cmd_dump]  - argc %d\n", cmd->runprogram.argc);
        for (int i = 0; i < cmd->runprogram.argc; i++) {
            co_printf("[cmd_dump]  - argv[%d] - [%s]\n", i, cmd->runprogram.argv[i]);
        }
        break;
    case CMDKIND_EMPTY:
        co_printf("[cmd_dump] EMPTY\n");
        break;
    default:
        co_printf("[cmd_dump] UNKNOWN CMD\n");
    }
}

static int cmd_exec(union shellcmd const *cmd) {
    switch (cmd->kind) {
    case CMDKIND_RUNPROGRAM: {
        assert(cmd->runprogram.argc != 0);
        assert(cmd->runprogram.argv != nullptr);
        struct shell_program *program_to_run = nullptr;
        LIST_FOREACH(&s_programs, programnode) {
            struct shell_program *program = programnode->data;
            if (kstrcmp(program->name, cmd->runprogram.argv[0]) == 0) {
                program_to_run = program;
                break;
            }
        }
        if (program_to_run == nullptr) {
            co_printf("%s: command not found\n", cmd->runprogram.argv[0]);
            return 127;
        }
        optind = 1;
        opterr = 1;
        return program_to_run->main(cmd->runprogram.argc, cmd->runprogram.argv);
    }
    case CMDKIND_EMPTY:
        return 0;
    default:
        panic("shell: cmd_exec internal error - unknown cmd type");
    }
}

static void register_program(struct shell_program *program) {
    list_insert_back(&s_programs, &program->node, program);
}

int shell_exec_cmd(char const *str) {
    int ret = 0;
    union shellcmd cmd;
    struct smatcher linematcher;
    smatcher_init(&linematcher, str);
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

void shell_repl(void) {
    char cmdline[SHELL_MAX_CMDLINE_LEN + 1];

    while (1) {
        size_t cursorpos = 0;
        cmdline[0] = '\0';
        co_printf("kernel> ");

        while (1) {
            int c = co_get_char();
            if (c == CON_BACKSPACE || c == CON_DELETE) {
                if (cursorpos != 0) {
                    cursorpos--;
                    co_printf("\b");
                }
            } else if ((c == '\r') || (c == '\n')) {
                cmdline[cursorpos] = '\0';
                co_printf("\n");
                break;
            } else {
                if (cursorpos < (SHELL_MAX_CMDLINE_LEN - 1)) {
                    cmdline[cursorpos] = (char)c;
                    co_printf("%c", c);
                    cursorpos++;
                }
            }
        }

        int ret = shell_exec_cmd(cmdline);
        if (ret != 0) {
            co_printf("command error %d\n", ret);
        }
    }
}

void shell_init(void) {
#define X(_x) register_program(&(_x));
    ENUMERATE_SHELLPROGRAMS(X)
#undef X
}
