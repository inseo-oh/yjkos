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

static struct stream s_stream;

static size_t s_totalcolumns, s_totalrows;
static uint16_t s_currentcolumn = 0, s_currentrow = 0;
static struct vt100tty_screenline *s_screenlines;
static void (*updatescreen)(struct vt100tty_screenline *s_screenlines);

static void advanceline(bool wastextoverflow) {
    s_currentcolumn = 0;
    s_currentrow++;
    while (s_totalrows <= s_currentrow) {
        for (size_t src_line = 1; src_line < s_totalrows; src_line++) {
            size_t dest_line = src_line - 1;
            memcpy(s_screenlines[dest_line].chars, s_screenlines[src_line].chars,  s_totalcolumns * sizeof(*s_screenlines[dest_line].chars));
            s_screenlines[dest_line].iscontinuation = s_screenlines[src_line].iscontinuation;
        }
        s_currentrow--;
        if (s_currentrow < s_totalrows) {
            memset(s_screenlines[s_currentrow].chars, ' ', s_totalcolumns * sizeof(*s_screenlines[s_currentrow].chars));
        }
    }
    s_screenlines[0].iscontinuation = false;
    s_screenlines[s_currentrow].iscontinuation = wastextoverflow;
}

static void writechar(char chr) {
    if (chr == '\n') {
        advanceline(false);
        return;
    } else if (chr == '\r') {
        s_currentcolumn = 0;
        return;
    } else if (s_totalcolumns <= s_currentcolumn) {
        advanceline(true);
    }
    s_screenlines[s_currentrow].chars[s_currentcolumn] = chr;
    s_currentcolumn++;
}

static WARN_UNUSED_RESULT ssize_t stream_op_write(
    struct stream *self, void *data, size_t size) {
    (void)self;
    assert(size <= STREAM_MAX_TRANSFER_SIZE);

    for (size_t idx = 0; idx < size; idx++) {
        uint8_t c = ((uint8_t *)data)[idx];
        writechar(c);
    }
    return size;
}

static WARN_UNUSED_RESULT ssize_t stream_op_read(
    struct stream *self, void *buf, size_t size) {
    (void)self;
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

static void stream_op_flush(struct stream *self) {
    (void)self;
    updatescreen(s_screenlines);
}

static struct stream_ops const OPS = {
    .write = stream_op_write,
    .read = stream_op_read,
    .flush = stream_op_flush,
};

void vt100tty_init(struct vt100tty_screenline *screenlines, size_t columns, size_t rows, void (*updatescreen_op)(struct vt100tty_screenline *s_screenlines)) {
    s_stream.data = NULL;
    s_stream.ops = &OPS;
    s_totalcolumns = columns;
    s_totalrows = rows;
    s_screenlines = screenlines;
    updatescreen = updatescreen_op;
 
    for (size_t r = 0; r < s_totalrows; r++) {
        memset(s_screenlines[s_currentrow].chars, ' ', s_totalcolumns * sizeof(*s_screenlines[s_currentrow].chars));
    }
    tty_setconsole(&s_stream);
}

