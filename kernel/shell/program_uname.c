#include "shell.h"
#include <dirent.h>
#include <stdint.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <kernel/version.h>
#include <kernel/fs/vfs.h>
#include <kernel/io/tty.h>
#include <kernel/lib/diagnostics.h>
#include <kernel/mem/heap.h>

// https://pubs.opengroup.org/onlinepubs/9799919799/utilities/ls.html

struct opts {
    bool machine : 1;
    bool node    : 1;
    bool release : 1;
    bool sysname    : 1;
    bool version : 1;
};

static WARN_UNUSED_RESULT bool getopts(
    struct opts *out, int argc, char *argv[])
{
    bool ok = true;
    int c;
    memset(out, 0, sizeof(*out));
    while (1) {
        c = getopt(argc, argv, "amnrsv");
        if (c == -1) {
            break;
        }
        switch(c) {
            case 'a':
                out->machine = true;
                out->node = true;
                out->release = true;
                out->sysname = true;
                out->version = true;
                break;
            case 'm':
                out->machine = true;
                break;
            case 'n':
                out->node = true;
                break;
            case 'r':
                out->release = true;
                break;
            case 's':
                out->sysname = true;
                break;
            case 'v':
                out->version = true;
                break;
            case '?':
            case ':':
                ok = false;
                break;
        }
    }
    return ok;
}

static int program_main(int argc, char *argv[]) {
    struct opts opts;
    if (!getopts(&opts, argc, argv)) {
        return 1;
    }
    if (argc == 1) {
        // No options were given
        opts.sysname = true;
    } else if (optind < argc) {
        tty_printf("%s: Extra operand %s", argv[0], argv[optind]);
        return 1;
    }
    if (opts.sysname) {
        tty_printf("YJKOS");
        if (opts.node || opts.release || opts.version || opts.machine) {
            tty_printf(" ");
        }
    }
    if (opts.node) {
        tty_printf("localhost");
        if (opts.release || opts.version || opts.machine) {
            tty_printf(" ");
        }
    }
    if (opts.release) {
        tty_printf(YJKOS_RELEASE);
        if (opts.version || opts.machine) {
            tty_printf(" ");
        }
    }
    if (opts.version) {
        tty_printf(YJKOS_VERSION);
        if (opts.machine) {
            tty_printf(" ");
        }
    }
    if (opts.machine) {
#if YJKERNEL_ARCH_X86
        tty_printf("i586");
#else
        #error Unknown arch
#endif
    }
    tty_printf("\n");

    return 0;
}

struct shell_program g_shell_program_uname = {
    .name = "uname",
    .main = program_main,
};
