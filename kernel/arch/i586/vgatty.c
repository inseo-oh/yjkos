#include "vgatty.h"
#include <kernel/io/co.h>
#include <kernel/io/kbd.h>
#include <kernel/io/stream.h>
#include <kernel/lib/diagnostics.h>
#include <kernel/lib/strutil.h>
#include <kernel/mem/vmm.h>
#include <kernel/types.h>
#include <stdint.h>
#include <sys/types.h>

struct chr {
    char chr;
    uint8_t attr;
};
STATIC_ASSERT_SIZE(struct chr, 2);

static struct stream s_stream;
static struct chr *s_chars;

static size_t s_totalcolumns, s_totalrows;
static uint16_t s_currentcolumn, s_currentrow;

static void write_char_at(uint16_t row, uint16_t col, char c) {
    s_chars[row * s_totalcolumns + col].chr = c;
}

static void write_attr_at(uint16_t row, uint16_t col, uint8_t attr) {
    s_chars[row * s_totalcolumns + col].attr = attr;
}

static void advance_line(void) {
    s_currentcolumn = 0;
    s_currentrow++;
    while (s_totalrows <= s_currentrow) {
        for (size_t src_line = 1; src_line < s_totalrows; src_line++) {
            size_t dest_line = src_line - 1;
            memcpy(&s_chars[dest_line * s_totalcolumns], &s_chars[src_line * s_totalcolumns], s_totalcolumns * sizeof(*s_chars));
        }
        s_currentrow--;
        if (s_currentrow < s_totalrows) {
            for (size_t i = 0; i < s_totalcolumns; i++) {
                write_char_at(s_currentrow, i, ' ');
            }
        }
    }
}

static void write_char(char chr) {
    if (chr == '\n') {
        advance_line();
        return;
    }
    if (chr == '\r') {
        s_currentcolumn = 0;
        return;
    }
    if (s_totalcolumns <= s_currentcolumn) {
        advance_line();
    }
    write_char_at(s_currentrow, s_currentcolumn, chr);
    s_currentcolumn++;
}

[[nodiscard]] static ssize_t stream_op_write(struct stream *self, void *data, size_t size) {
    (void)self;
    assert(size <= STREAM_MAX_TRANSFER_SIZE);

    for (size_t idx = 0; idx < size; idx++) {
        char c = ((char *)data)[idx];
        write_char(c);
    }
    return (ssize_t)size;
}

[[nodiscard]] static ssize_t stream_op_read(struct stream *self, void *buf, size_t size) {
    (void)self;
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
            /* TODO: Translate keycodes to ANSI terminal codes. */
            continue;
        }
        *((uint8_t *)buf) = event.chr;
        read_len++;
    }
    return (ssize_t)read_len;
}

static struct stream_ops const OPS = {
    .write = stream_op_write,
    .read = stream_op_read,
};

void archi586_vgatty_init_early_debug(void) {
    s_stream.data = NULL;
    s_stream.ops = &OPS;
    s_totalcolumns = 80;
    s_totalrows = 25;

    s_chars = (void *)0xb8000;
    co_set_debug_console(&s_stream);
}

void archi586_vgatty_init(PHYSPTR baseaddr, size_t columns, size_t rows, size_t bytes_per_row) {
    s_stream.data = NULL;
    s_stream.ops = &OPS;
    s_totalcolumns = columns;
    s_totalrows = rows;
    assert(columns * 2 == bytes_per_row);

    s_chars = vmm_ezmap(baseaddr, s_totalcolumns * s_totalrows * 2);
    for (size_t r = 0; r < s_totalrows; r++) {
        for (size_t c = 0; c < s_totalcolumns; c++) {
            write_char_at(r, c, ' ');
            write_attr_at(r, c, 0x07);
        }
    }
    co_set_primary_console(&s_stream);
}
