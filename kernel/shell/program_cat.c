#include "shell.h"
#include <kernel/fs/vfs.h>
#include <kernel/io/co.h>
#include <kernel/lib/diagnostics.h>
#include <kernel/lib/strutil.h>
#include <sys/types.h>
#include <unistd.h>

/* https://pubs.opengroup.org/onlinepubs/9799919799/utilities/ls.html */

struct opts {
    bool dummy;
};

[[nodiscard]] static bool getopts(struct opts *out, int argc, char *argv[]) {
    bool ok = true;
    int c;
    vmemset(out, 0, sizeof(*out));
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
    struct file *fd = nullptr;
    ssize_t ret = vfs_open_file(&fd, path, 0);
    if (ret < 0) {
        co_printf("%s: failed to open directory %s (error %d)\n", progname, path, ret);
        return;
    }
    while (1) {
        char buf[1024];
        ret = vfs_read_file(fd, buf, sizeof(buf));
        if (ret == 0) {
            break;
        }
        if (ret < 0) {
            co_printf("%s: failed to read file %s (error %d)\n", progname, path, ret);
            break;
        }
        for (ssize_t i = 0; i < ret; i++) {
            co_printf("%c", buf[i]);
        }
    }
}

static int program_main(int argc, char *argv[]) {
    struct opts opts;
    if (!getopts(&opts, argc, argv)) {
        return 1;
    }
    if (argc <= optind) {
        co_printf("%s: reading from stdin is not supported yet\n", argv[0]);
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
