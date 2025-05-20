#include "fbtty.h"
#include "psf.h"
#include <assert.h>
#include <kernel/io/co.h>
#include <kernel/io/vt100tty.h>
#include <kernel/mem/heap.h>
#include <kernel/raster/fb.h>
#include <stddef.h>
#include <stdint.h>

static struct vt100tty s_tty;
static char *s_linetempbuf;

static void vt100tty_op_updatescreen(struct vt100tty *self) {
    assert(s_linetempbuf);
    int writepos = 0;
    int writelen = 0;
    struct vt100tty_char *src = self->chars;
    for (int32_t row = 0; row < self->rows; row++) {
        for (int32_t col = 0; col < self->columns; col++, src++) {
            if (src->need_supdate) {
                if (writelen == 0) {
                    writepos = col;
                }
                s_linetempbuf[writelen] = src->chr;
                src->need_supdate = false;
                writelen++;
            }
            if ((!src->need_supdate || col == (self->columns - 1)) && (writelen != 0)) {
                s_linetempbuf[writelen] = '\0';
                int destx = writepos * psf_getwidth();
                int desty = row * psf_getheight();
                fb_draw_rect(writelen * psf_getwidth(), psf_getheight(), destx, desty, black());
                fb_draw_text(s_linetempbuf, destx, desty, white());
                writelen = 0;
            }
        }
    }
    fb_update();
}

static void vt100tty_op_scroll(struct vt100tty *self, int lines) {
    (void)self;
    fb_scroll(lines * psf_getheight());
}

static const struct vt100tty_ops OPS = {
    .update_screen = vt100tty_op_updatescreen,
    .scroll = vt100tty_op_scroll,
};

void fbtty_init(void) {
    fb_draw_rect(fb_get_width(), fb_get_height(), 0, 0, black());

    int32_t columns = fb_get_width() / psf_getwidth();
    int32_t rows = fb_get_height() / psf_getheight();
    struct vt100tty_line_info *lineinfos = heap_calloc(sizeof(*lineinfos), rows, HEAP_FLAG_ZEROMEMORY);
    struct vt100tty_char *chars = heap_calloc(sizeof(*chars), columns * rows, HEAP_FLAG_ZEROMEMORY);
    s_linetempbuf = heap_calloc(sizeof(*s_linetempbuf), columns + 1, HEAP_FLAG_ZEROMEMORY);
    if ((lineinfos == NULL) || (chars == NULL) || (s_linetempbuf == NULL)) {
        goto oom;
    }
    vt100tty_init(&s_tty, lineinfos, chars, &OPS, columns, rows);
    return;
oom:
    co_printf("fbtty: not enough memory to initialize\n");
    heap_free(lineinfos);
    heap_free(chars);
    heap_free(s_linetempbuf);
}
