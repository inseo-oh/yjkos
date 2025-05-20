#include <assert.h>
#include <kernel/io/co.h>
#include <kernel/io/kbd.h>
#include <kernel/io/stream.h>
#include <kernel/io/vt100tty.h>
#include <kernel/lib/diagnostics.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>

static struct vt100tty_char *char_at(struct vt100tty *self, int row, int column) {
    assert(0 <= row);
    assert(0 <= column);
    assert(row < self->rows);
    assert(column < self->columns);
    return &self->chars[(row * self->columns) + column];
}

static void advanceline(struct vt100tty *self, bool wastextoverflow) {
    self->currentcolumn = 0;
    self->currentrow++;
    bool hasscroll = self->ops->scroll != NULL;
    while (self->rows <= self->currentrow) {
        for (int srcline = 1; srcline < self->rows; srcline++) {
            int destline = srcline - 1;
            struct vt100tty_char *dest = char_at(self, destline, 0);
            struct vt100tty_char *src = char_at(self, srcline, 0);
            memcpy(dest, src, self->columns * sizeof(*self->chars));
            if (!hasscroll) {
                for (int col = 0; col < self->columns; col++) {
                    dest[col].need_supdate = true;
                    src[col].need_supdate = true;
                }
            }
            self->lineinfos[destline].is_continuation = self->lineinfos[srcline].is_continuation;
            if (!hasscroll) {
                self->lineinfos[destline].need_supdate = true;
            }
        }
        self->currentrow--;
        if (hasscroll) {
            self->ops->scroll(self, 1);
        }
        if (self->currentrow < self->rows) {
            struct vt100tty_char *dest = char_at(self, self->currentrow, 0);
            for (int col = 0; col < self->columns; col++) {
                dest[col].chr = ' ';
                dest[col].need_supdate = true;
            }
            self->lineinfos[self->currentrow].need_supdate = true;
        }
    }
    self->lineinfos[0].is_continuation = false;
    self->lineinfos[self->currentrow].is_continuation = wastextoverflow;
}

static void write_char(struct vt100tty *self, int chr) {
    if (chr == '\n') {
        advanceline(self, false);
        return;
    }
    if (chr == '\r') {
        self->currentcolumn = 0;
        return;
    }
    if (self->columns <= self->currentcolumn) {
        advanceline(self, true);
    }
    struct vt100tty_char *dest = char_at(self, self->currentrow, self->currentcolumn);
    dest->chr = (char)chr;
    dest->need_supdate = true;
    self->lineinfos[self->currentrow].need_supdate = true;
    self->currentcolumn++;
}

[[nodiscard]] static ssize_t stream_op_write(struct stream *stream, void *data, size_t size) {
    struct vt100tty *self = stream->data;
    assert(size <= STREAM_MAX_TRANSFER_SIZE);

    for (size_t idx = 0; idx < size; idx++) {
        int c = ((uint8_t *)data)[idx];
        write_char(self, c);
    }
    return (ssize_t)size;
}

[[nodiscard]] static ssize_t stream_op_read(struct stream *stream, void *buf, size_t size) {
    (void)stream;
    assert(size <= STREAM_MAX_TRANSFER_SIZE);

    size_t read_len = 0;
    for (size_t idx = 0; idx < size; idx++) {
        struct kbd_key_event event;
        if (!kbd_pull_event(&event)) {
            break;
        }
        if (!event.is_down) {
            continue;
        }
        if (event.chr == 0) {
            /* TODO: Translate keycodes to ANSI terminal codes. ****************/
            continue;
        }
        *((uint8_t *)buf) = event.chr;
        read_len++;
    }
    return (ssize_t)read_len;
}

static void stream_op_flush(struct stream *stream) {
    (void)stream;
    struct vt100tty *self = stream->data;
    self->ops->update_screen(self);
    for (int r = 0; r < self->rows; r++) {
        self->lineinfos[self->currentrow].need_supdate = false;
    }
}

static struct stream_ops const OPS = {
    .write = stream_op_write,
    .read = stream_op_read,
    .flush = stream_op_flush,
};

void vt100tty_init(struct vt100tty *out, struct vt100tty_line_info *lineinfos, struct vt100tty_char *chars, struct vt100tty_ops const *ops, int columns, int rows) {
    out->stream.data = out;
    out->stream.ops = &OPS;
    out->columns = columns;
    out->rows = rows;
    out->lineinfos = lineinfos;
    out->ops = ops;
    out->chars = chars;

    for (int r = 0; r < out->rows; r++) {
        for (int c = 0; c < out->columns; c++) {
            char_at(out, r, c)->chr = ' ';
            char_at(out, r, c)->need_supdate = true;
        }
        out->lineinfos[r].need_supdate = true;
    }
    co_set_primary_console(&out->stream);
}
