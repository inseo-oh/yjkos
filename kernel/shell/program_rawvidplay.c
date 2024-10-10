#include "shell.h"
#include <assert.h>
#include <kernel/io/tty.h>
#include <kernel/io/vfs.h>
#include <kernel/raster/fb.h>
#include <kernel/status.h>

enum {
    FRAME_SIZE = 640 * 480 * 2
};
static fb_color_t s_framebuffer[FRAME_SIZE];

SHELLFUNC static int program_main(int argc, char *argv[]) {
    if (argc < 2) {
        tty_printf("usage: rawvidplay <rawvideo file>\n");
        return 1;
    }
    fd_t *fd;
    status_t status = vfs_openfile(&fd, argv[1], 0);
    if (status != OK) {
        tty_printf("can't open file\n");
        return 1;
    }
    for (size_t i = 0; ; i++) {
        size_t fsize = FRAME_SIZE;
        status_t status = vfs_readfile(fd, s_framebuffer, &fsize);
        if (status == ERR_EOF) {
            tty_printf("thanks\n");
            break;
        }
        assert(fsize == FRAME_SIZE);
        if (status != OK) {
            tty_printf("Frame %d Read FAILED\n", i);
            break;
        }
        fb_drawimage(s_framebuffer, 640, 480, 640, 0, 0);
        fb_update();
    }
    return 0;
}

SHELLDATA shell_program_t g_shell_program_rawvidplay = {
    .name = "rawvidplay",
    .main = program_main,
};

