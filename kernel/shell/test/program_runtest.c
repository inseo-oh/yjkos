#include "../shell.h"
#include "test.h"
#include <kernel/io/tty.h>
#include <string.h>

SHELLRODATA static struct testgroup const * const TESTGROUPS[] = {
    // lib
    &TESTGROUP_BITMAP,
    &TESTGROUP_BST,
    &TESTGROUP_LIST,
    &TESTGROUP_QUEUE,

    // mem
    &TESTGROUP_PMM,
    &TESTGROUP_HEAP,
};

SHELLFUNC static bool runtests(struct testgroup const *group) {
    size_t okcount = 0, failcount = 0;
    tty_printf("running test group '%s' (%zu tests)\n", group->name, group->testslen);
    for (size_t i = 0; i < group->testslen; i++) {
        tty_printf("[test %u / %u] %s\n", i + 1, group->testslen, group->tests[i].name);
        if (!group->tests[i].fn()) {
            failcount++;
        } else {
            okcount++;
        }
    }
    tty_printf("finished test group '%s' (%zu tests, %zu passed, %zu failed)\n", group->name, group->testslen, okcount, failcount);
    return failcount == 0;
}

SHELLRODATA static char const * const HELP = 
    "Usage: test <options> <testgroups...>\n"
    "\n"
    "OPTIONS:\n"
    " -h, --help: Shows this help and exit\n"
    " -l, --list: Lists available testgroups and exit\n"
    " -a, --all : Runs all testgroups\n"
    ;

SHELLFUNC static int program_main(int argc, char *argv[]) {
    // Check options
    bool opt_help = false;
    bool opt_list = false;
    bool opt_all = false;
    for (int i = 1; i < argc; i++) {
        if ((strcmp(argv[i], "-h") == 0) || (strcmp(argv[i], "--help") == 0)) {
            opt_help = true;
        } else if ((strcmp(argv[i], "-l") == 0) || (strcmp(argv[i], "--list") == 0)) {
            opt_list = true;
        } else if ((strcmp(argv[i], "-a") == 0) || (strcmp(argv[i], "--all") == 0)) {
            opt_all = true;
        } else if (argv[i][0] == '-') {
            tty_printf("Unrecognized option %s\n", argv[i]);
            return 1;
        }
    }
    // Process options
    if (opt_help) {
        tty_printf("%s", HELP);
        return 0;
    }
    if (opt_list) {
        for (size_t i = 0; i < sizeof(TESTGROUPS)/sizeof(void *); i++) {
            tty_printf("test group '%s' (%zu tests)\n", TESTGROUPS[i]->name, TESTGROUPS[i]->testslen);
        }
        return 0;
    }
    if (opt_all) {
        for (size_t i = 0; i < sizeof(TESTGROUPS)/sizeof(void *); i++) {
            if (!runtests(TESTGROUPS[i])) {
                return 1;
            }
        }
        goto testdone;
    }
    // Run tests
    bool notests = true;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            continue;
        }
        notests = false;
        struct testgroup const *group = NULL;
        for (size_t j = 0; j < sizeof(TESTGROUPS)/sizeof(void *); j++) {
            if (strcmp(argv[i], TESTGROUPS[j]->name) == 0) {
                group = TESTGROUPS[j];
            }
        }
        if (group == NULL) {
            tty_printf("No testgroup named %s exists - Run `runtest -l` for testgroup list\n", argv[i]);
            return 1;
        }
        if (!runtests(group)) {
            return 1;
        }
    }
    if (notests) {
        tty_printf("No test or options specified - Run `runtest -h` for help\n");
        return 1;
    }
testdone:
    tty_printf("test OK\n");
    return 0;
}

SHELLDATA struct shell_program g_shell_program_runtest = {
    .name = "runtest",
    .main = program_main,
};
