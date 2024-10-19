#include "shell.h"
#include <dirent.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <kernel/fs/vfs.h>
#include <kernel/io/tty.h>
#include <kernel/lib/diagnostics.h>
#include <kernel/mem/heap.h>

// https://pubs.opengroup.org/onlinepubs/9799919799/utilities/ls.html

struct opts {
    bool dummy;
};

static WARN_UNUSED_RESULT bool getopts(
    struct opts *out, int argc, char *argv[])
{
    bool ok = true;
    int c;
    memset(out, 0, sizeof(*out));
    while (1) {
        c = getopt(argc, argv, "u");
        if (c == -1) {
            break;
        }
        switch(c) {
            case 'u':
                break;
            case '?':
            case ':':
                ok = false;
                break;
        }
    }
    return ok;
}

static void showfile(
    char const *progname, char const *path, struct opts const *opts)
{
    (void)opts;
    struct fd *fd;
    ssize_t ret = vfs_openfile(&fd, path, 0);
    if (ret < 0) {
        tty_printf(
            "%s: failed to open directory %s (error %d)\n",
            progname, path, ret);
        return;
    }
    while (1) {
        char buf[1024];
        ret = vfs_readfile(fd, buf, sizeof(buf));
        if (ret == 0) {
            break;
        } else if (ret < 0) {
            tty_printf(
                "%s: failed to read file %s (error %d)\n",
                progname, path, ret);
            break;
        }
        for (ssize_t i = 0; i < ret; i++) {
            tty_printf("%c", buf[i]);
        }
    }
}

static int program_main(int argc, char *argv[]) {
    struct opts opts;
    if (!getopts(&opts, argc, argv)) {
        return 1;
    }
    if (argc <= optind) {
        tty_printf(
            "%s: reading from stdin is not supported yet\n", argv[0]);
        return 1;
    }
    for (int i = optind; i < argc; i++) {
        showfile(argv[0], argv[i], &opts);
    }
    return 0;
}

struct shell_program g_shell_program_cat = {
    .name = "cat",
    .main = program_main,
};
