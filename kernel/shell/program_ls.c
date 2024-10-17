#include "shell.h"
#include <errno.h>
#include <dirent.h>
#include <kernel/fs/vfs.h>
#include <kernel/io/stream.h>
#include <kernel/io/tty.h>
#include <kernel/raster/fb.h>

static void showdir(char const *path) {
    DIR *dir;
    int ret = vfs_opendir(&dir, path);
    if (ret < 0) {
        tty_printf(
            "failed to open directory %s (error %d)\n", path, ret);
        return;
    }
    while (1) {
        struct dirent ent;
        ret = vfs_readdir(&ent, dir);
        if (ret == -ENOENT) {
            break;
        } else if (ret != 0) {
            tty_printf(
                "failed to read directory %s (error %d)\n", path, ret);
            break;
        }
        tty_printf("%s  ", ent.d_name);
    }
    tty_printf("\n");
    vfs_closedir(dir);
}

static int program_main(int argc, char *argv[]) {
    if (argc < 2) {
        showdir("/");
        return 0;
    }
    for (int i = 1; i < argc; i++) {
        if (argc != 2) {
            tty_printf("%s:\n", argv[i]);
        }
        showdir(argv[i]);
    }
    return 0;
}

struct shell_program g_shell_program_ls = {
    .name = "ls",
    .main = program_main,
};

