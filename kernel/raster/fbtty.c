#include "fbtty.h"
#include "psf.h"
#include <assert.h>
#include <kernel/io/co.h>
#include <kernel/io/vt100tty.h>
#include <kernel/mem/heap.h>
#include <kernel/raster/fb.h>
#include <stddef.h>
#include <stdint.h>

static struct Vt100Tty s_tty;
static char *s_linetempbuf;

static void vt100tty_op_updatescreen(struct Vt100Tty *self) {
    assert(s_linetempbuf);
    int writepos = 0;
    int writelen = 0;
    struct Vt100Tty_Char *src = self->chars;
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
                Fb_DrawRect(writelen * psf_getwidth(), psf_getheight(), destx, desty, Black());
                Fb_DrawText(s_linetempbuf, destx, desty, White());
                writelen = 0;
            }
        }
    }
    Fb_Update();
}

static void vt100tty_op_scroll(struct Vt100Tty *self, int lines) {
    (void)self;
    Fb_Scroll(lines * psf_getheight());
}

static const struct Vt100TtyOps OPS = {
    .UpdateScreen = vt100tty_op_updatescreen,
    .Scroll = vt100tty_op_scroll,
};

void fbtty_init(void) {
    Fb_DrawRect(Fb_GetWidth(), Fb_GetHeight(), 0, 0, Black());

    int32_t columns = Fb_GetWidth() / psf_getwidth();
    int32_t rows = Fb_GetHeight() / psf_getheight();
    struct Vt100Tty_LineInfo *lineinfos = Heap_Calloc(sizeof(*lineinfos), rows, HEAP_FLAG_ZEROMEMORY);
    struct Vt100Tty_Char *chars = Heap_Calloc(sizeof(*chars), columns * rows, HEAP_FLAG_ZEROMEMORY);
    s_linetempbuf = Heap_Calloc(sizeof(*s_linetempbuf), columns + 1, HEAP_FLAG_ZEROMEMORY);
    if ((lineinfos == NULL) || (chars == NULL) || (s_linetempbuf == NULL)) {
        goto oom;
    }
    Vt100tty_Init(&s_tty, lineinfos, chars, &OPS, columns, rows);
    return;
oom:
    Co_Printf("fbtty: not enough memory to initialize\n");
    Heap_Free(lineinfos);
    Heap_Free(chars);
    Heap_Free(s_linetempbuf);
}
