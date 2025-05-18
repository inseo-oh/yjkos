#include "shell.h"
#include <kernel/fs/vfs.h>
#include <kernel/io/co.h>
#include <kernel/lib/diagnostics.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

/* https://pubs.opengroup.org/onlinepubs/9799919799/utilities/ls.html */

struct opts {
    bool dummy;
};

[[nodiscard]] static bool getopts(struct opts *out, int argc, char *argv[]) {
    bool ok = true;
    int c;
    memset(out, 0, sizeof(*out));
    while (1) {
        c = getopt(argc, argv, "u");
        if (c == -1) {
            break;
        }
        switch (c) {
        case 'u':
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

static void showfile(char const *progname, char const *path, struct opts const *opts) {
    (void)opts;
    struct File *fd = NULL;
    ssize_t ret = Vfs_OpenFile(&fd, path, 0);
    if (ret < 0) {
        Co_Printf("%s: failed to open directory %s (error %d)\n", progname, path, ret);
        return;
    }
    while (1) {
        char buf[1024];
        ret = Vfs_ReadFile(fd, buf, sizeof(buf));
        if (ret == 0) {
            break;
        }
        if (ret < 0) {
            Co_Printf("%s: failed to read file %s (error %d)\n", progname, path, ret);
            break;
        }
        for (ssize_t i = 0; i < ret; i++) {
            Co_Printf("%c", buf[i]);
        }
    }
}

static int program_main(int argc, char *argv[]) {
    struct opts opts;
    if (!getopts(&opts, argc, argv)) {
        return 1;
    }
    if (argc <= optind) {
        Co_Printf("%s: reading from stdin is not supported yet\n", argv[0]);
        return 1;
    }
    for (int i = optind; i < argc; i++) {
        showfile(argv[0], argv[i], &opts);
    }
    return 0;
}

struct Shell_Program g_shell_program_cat = {
    .name = "cat",
    .main = program_main,
};
