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
        Co_Printf("usage: rawvidplay <rawvideo file>\n");
        return 1;
    }
    struct File *fd = NULL;
    int ret = Vfs_OpenFile(&fd, argv[1], 0);
    if (ret < 0) {
        Co_Printf("can't open file\n");
        return 1;
    }
    for (size_t i = 0;; i++) {
        ret = Vfs_ReadFile(fd, s_framebuffer, FRAME_SIZE);
        if (ret == 0) {
            Co_Printf("thanks\n");
            break;
        }
        assert(ret == FRAME_SIZE);
        if (ret < 0) {
            Co_Printf("Frame %d Read FAILED\n", i);
            break;
        }
        Fb_DrawImage(s_framebuffer, 640, 480, 640, 0, 0);
        fb_update();
    }
    return 0;
}

struct Shell_Program g_shell_program_rawvidplay = {
    .name = "rawvidplay",
    .main = program_main,
};
