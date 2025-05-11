#include "shell.h"
#include <assert.h>
#include <kernel/fs/vfs.h>
#include <kernel/io/co.h>
#include <kernel/raster/fb.h>
#include <stddef.h>

#define FRAME_SIZE (640 * 480 * 2)

static FB_COLOR s_framebuffer[FRAME_SIZE];

static int program_main(int argc, char *argv[]) {
    if (argc < 2) {
        co_printf("usage: rawvidplay <rawvideo file>\n");
        return 1;
    }
    struct fd *fd = NULL;
    int ret = vfs_open_file(&fd, argv[1], 0);
    if (ret < 0) {
        co_printf("can't open file\n");
        return 1;
    }
    for (size_t i = 0;; i++) {
        ret = vfs_readfile(fd, s_framebuffer, FRAME_SIZE);
        if (ret == 0) {
            co_printf("thanks\n");
            break;
        }
        assert(ret == FRAME_SIZE);
        if (ret < 0) {
            co_printf("Frame %d Read FAILED\n", i);
            break;
        }
        fb_draw_image(s_framebuffer, 640, 480, 640, 0, 0);
        fb_update();
    }
    return 0;
}

struct shell_program g_shell_program_rawvidplay = {
    .name = "rawvidplay",
    .main = program_main,
};
