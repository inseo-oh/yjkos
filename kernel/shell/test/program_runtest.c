#include "../shell.h"
#include "test.h"
#include <kernel/io/co.h>
#include <kernel/lib/diagnostics.h>
#include <kernel/lib/strutil.h>
#include <stddef.h>
#include <unistd.h>

static struct test_group const *const TEST_GROUPS[] = {
#define X(_x) &(_x),
    ENUMERATE_TESTGROUPS(X)
#undef X
};

static bool runtests(struct test_group const *group) {
    size_t okcount = 0;
    size_t failcount = 0;
    co_printf("running test group '%s' (%zu tests)\n", group->name, group->testslen);
    for (size_t i = 0; i < group->testslen; i++) {
        co_printf("[test %u / %u] %s\n", i + 1, group->testslen, group->tests[i].name);
        if (!group->tests[i].fn()) {
            failcount++;
        } else {
            okcount++;
        }
    }
    co_printf("finished test group '%s' (%zu tests, %zu passed, %zu failed)\n", group->name, group->testslen, okcount, failcount);
    return failcount == 0;
}

struct opts {
    bool list : 1;
    bool all : 1;
};

[[nodiscard]] static bool getopts(struct opts *out, int argc, char *argv[]) {
    bool ok = true;
    int c = 0;
    vmemset(out, 0, sizeof(*out));
    while (1) {
        c = getopt(argc, argv, "hla");
        if (c == -1) {
            break;
        }
        switch (c) {
        case 'l':
            out->list = true;
            break;
        case 'a':
            out->all = true;
            break;
        case '?':
        case ':':
            ok = false;
            break;
        default:
            assert(false);
        }
    }
    return ok;
}

static int program_main(int argc, char *argv[]) {
    struct opts opts;
    bool argok = getopts(&opts, argc, argv);
    if (!argok) {
        return 1;
    }
    if (opts.list) {
        for (size_t i = 0; i < sizeof(TEST_GROUPS) / sizeof(void *); i++) {
            co_printf("test group '%s' (%zu tests)\n", TEST_GROUPS[i]->name, TEST_GROUPS[i]->testslen);
        }
        return 0;
    }
    if (opts.all) {
        for (size_t i = 0; i < sizeof(TEST_GROUPS) / sizeof(void *); i++) {
            if (!runtests(TEST_GROUPS[i])) {
                return 1;
            }
        }
        goto testdone;
    }
    /* Run tests **************************************************************/
    bool notests = true;
    for (int i = optind; i < argc; i++) {
        notests = false;
        struct test_group const *group = nullptr;
        for (size_t j = 0; j < sizeof(TEST_GROUPS) / sizeof(void *); j++) {
            if (kstrcmp(argv[i], TEST_GROUPS[j]->name) == 0) {
                group = TEST_GROUPS[j];
            }
        }
        if (group == nullptr) {
            co_printf("No testgroup named %s exists - Run `runtest -l` for testgroup list\n", argv[i]);
            return 1;
        }
        if (!runtests(group)) {
            return 1;
        }
    }
    if (notests) {
        co_printf("No test or options specified - Run `runtest -h` for help\n");
        return 1;
    }
testdone:
    co_printf("test OK\n");
    return 0;
}

struct shell_program g_shell_program_runtest = {
    .name = "runtest",
    .main = program_main,
};
