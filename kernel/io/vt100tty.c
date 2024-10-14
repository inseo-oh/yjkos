#include <kernel/io/kbd.h>
#include <kernel/io/stream.h>
#include <kernel/io/tty.h>
#include <kernel/io/vt100tty.h>
#include <kernel/lib/diagnostics.h>
#include <kernel/mem/vmm.h>
#include <kernel/types.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>

static struct vt100tty_char *charat(struct vt100tty *self, int row, int column)
{
    return &self->chars[(row * self->columns) + column];
}

static void advanceline(struct vt100tty *self, bool wastextoverflow) {
    self->currentcolumn = 0;
    self->currentrow++;
    bool hasscroll = self->ops->scroll != NULL;
    while (self->rows <= self->currentrow) {
        for (int srcline = 1; srcline < self->rows; srcline++) {
            int destline = srcline - 1;
            struct vt100tty_char *dest = charat(self, destline, 0);
            struct vt100tty_char *src = charat(self, srcline, 0);
            memcpy(
               dest, src, self->columns * sizeof(*self->chars));
            if (!hasscroll) {
                for (int col = 0; col < self->columns; col++) {
                    dest[col].needsupdate = true;
                    src[col].needsupdate = true;
                }
            }
            self->lineinfos[destline].iscontinuation =
                self->lineinfos[srcline].iscontinuation;
            self->lineinfos[destline].needsupdate |= !hasscroll;
        }
        self->currentrow--;
        if (hasscroll) {
            self->ops->scroll(self, 1);
        }
        if (self->currentrow < self->rows) {
            struct vt100tty_char *dest =
                charat(self, self->currentrow, 0);
            for (int col = 0; col < self->columns; col++) {
                dest[col].chr = ' ';
                dest[col].needsupdate = true;
            }
            self->lineinfos[self->currentrow].needsupdate = true;
        }
    }
    self->lineinfos[0].iscontinuation = false;
    self->lineinfos[self->currentrow].iscontinuation = wastextoverflow;
}

static void writechar(struct vt100tty *self, char chr) {
    if (chr == '\n') {
        advanceline(self, false);
        return;
    } else if (chr == '\r') {
        self->currentcolumn = 0;
        return;
    } else if (self->columns <= self->currentcolumn) {
        advanceline(self, true);
    }
    struct vt100tty_char *dest =
        charat(self, self->currentrow, self->currentcolumn);
    dest->chr = chr;
    dest->needsupdate = true;
    self->lineinfos[self->currentrow].needsupdate = true;
    self->currentcolumn++;
}

static WARN_UNUSED_RESULT ssize_t stream_op_write(
    struct stream *stream, void *data, size_t size)
{
    struct vt100tty *self = stream->data;
    assert(size <= STREAM_MAX_TRANSFER_SIZE);

    for (size_t idx = 0; idx < size; idx++) {
        uint8_t c = ((uint8_t *)data)[idx];
        writechar(self, c);
    }
    return size;
}

static WARN_UNUSED_RESULT ssize_t stream_op_read(
    struct stream *stream, void *buf, size_t size)
{
    (void)stream;
    assert(size <= STREAM_MAX_TRANSFER_SIZE);

    size_t read_len = 0;
    for (size_t idx = 0; idx < size; idx++) {
        struct kbd_keyevent event;
        if (!kbd_pullevent(&event)) {
            break;
        }
        if (!event.is_down) {
            continue;
        }
        if (event.chr == 0) {
            // TODO: Translate keycodes to ANSI terminal codes.
            continue;
        }
        *((uint8_t *)buf) = event.chr;
        read_len++;
    }
    return read_len;
}

static void stream_op_flush(struct stream *stream) {
    (void)stream;
    struct vt100tty *self = stream->data;
    self->ops->updatescreen(self);
    for (int r = 0; r < self->rows; r++) {
        self->lineinfos[self->currentrow].needsupdate = false;
    }
}

static struct stream_ops const OPS = {
    .write = stream_op_write,
    .read = stream_op_read,
    .flush = stream_op_flush,
};

void vt100tty_init(
    struct vt100tty *out, struct vt100tty_lineinfo *lineinfos,
    struct vt100tty_char *chars, struct vt100tty_ops const *ops,
    int columns, int rows)
{
    out->stream.data = out;
    out->stream.ops = &OPS;
    out->columns = columns;
    out->rows = rows;
    out->lineinfos = lineinfos;
    out->ops = ops;
    out->chars = chars;

    for (int r = 0; r < out->rows; r++) {
        for (int c = 0; c < out->columns; c++) {
            charat(out, r, c)->chr = ' ';
            charat(out, r, c)->needsupdate = true;
        }
        out->lineinfos[r].needsupdate = true;
    }
    tty_setconsole(&out->stream);
}
