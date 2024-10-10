#include "fbtty.h"
#include "psf.h"
#include <kernel/io/tty.h>
#include <kernel/io/vt100tty.h>
#include <kernel/mem/heap.h>
#include <kernel/raster/fb.h>
#include <stdint.h>
#include <stddef.h>

static int32_t s_rows;

static void updatescreen_op(vt100tty_screenline_t *screenlines) {
    fb_drawrect(fb_getwidth(), fb_getheight(), 0, 0, black());
    for (int32_t i = 0; i < s_rows; i++) {
        fb_drawtext(screenlines[i].chars, 0, i * psf_getheight(), white());
    }
    fb_update();
}

void fbtty_init(void) {
    int32_t columns = fb_getwidth() / psf_getwidth();
    int32_t rows = fb_getheight() / psf_getheight();
    s_rows = rows;
    vt100tty_screenline_t *screenlines = heap_calloc(sizeof(*screenlines), rows, HEAP_FLAG_ZEROMEMORY);
    if (screenlines == NULL) {
        goto oom;
    }
    for (int32_t i = 0; i < rows; i++) {
        // We allocate one byte more than `columns`, that will act as NULL terminator for each line.
        screenlines[i].chars = heap_calloc(sizeof(*screenlines[i].chars), columns + 1, HEAP_FLAG_ZEROMEMORY);
        if (screenlines[i].chars == NULL) {
            goto oom;
        }
    }

    vt100tty_init(screenlines, columns, rows, updatescreen_op);
    return;
oom:
    tty_printf("fbtty: not enough memory to initialize\n");
    for (int32_t i = 0; i < rows; i++) {
        heap_free(screenlines[i].chars);
    }
}

