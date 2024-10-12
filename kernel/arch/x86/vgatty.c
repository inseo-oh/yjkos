#include "vgatty.h"
#include <kernel/io/kbd.h>
#include <kernel/io/stream.h>
#include <kernel/io/tty.h>
#include <kernel/lib/diagnostics.h>
#include <kernel/mem/vmm.h>
#include <kernel/status.h>
#include <kernel/types.h>
#include <stdint.h>
#include <string.h>

struct chr {
    char chr;
    uint8_t attr;
};
STATIC_ASSERT_SIZE(struct chr, 2);


static struct stream s_stream;
static struct chr *s_chars;

static size_t s_totalcolumns, s_totalrows;
static uint16_t s_currentcolumn, s_currentrow;

static void writecharat(uint16_t row, uint16_t col, char c) {
    s_chars[row * s_totalcolumns + col].chr = c;
}

static void writeattrat(uint16_t row, uint16_t col, uint8_t attr) {
    s_chars[row * s_totalcolumns + col].attr = attr;
}

static void advanceline(void) {
    s_currentcolumn = 0;
    s_currentrow++;
    while (s_totalrows <= s_currentrow) {
        for (size_t src_line = 1; src_line < s_totalrows; src_line++) {
            size_t dest_line = src_line - 1;
            memcpy(&s_chars[dest_line * s_totalcolumns], &s_chars[src_line * s_totalcolumns],  s_totalcolumns * sizeof(*s_chars));
        }
        s_currentrow--;
        if (s_currentrow < s_totalrows) {
            for (size_t i = 0; i < s_totalcolumns; i++) {
                writecharat(s_currentrow, i, ' ');
            }
        }
    }
}

static void writechar(char chr) {
    if (chr == '\n') {
        advanceline();
        return;
    } else if (chr == '\r') {
        s_currentcolumn = 0;
        return;
    } else if (s_totalcolumns <= s_currentcolumn) {
        advanceline();
    }
    writecharat(s_currentrow, s_currentcolumn, chr);
    s_currentcolumn++;
}


static FAILABLE_FUNCTION stream_op_write(struct stream *self, void *data, size_t size) {
FAILABLE_PROLOGUE
    (void)self;

    for (size_t idx = 0; idx < size; idx++) {
        uint8_t c = ((uint8_t *)data)[idx];
        writechar(c);
    }
FAILABLE_EPILOGUE_BEGIN
FAILABLE_EPILOGUE_END
}

static FAILABLE_FUNCTION stream_op_read(size_t *size_out, struct stream *self, void *buf, size_t size) {
FAILABLE_PROLOGUE
    (void)self;
    
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
    *size_out = read_len;
FAILABLE_EPILOGUE_BEGIN
FAILABLE_EPILOGUE_END
}

static struct stream_ops const OPS = {
    .write = stream_op_write,
    .read = stream_op_read,
};

void archx86_vgatty_init_earlydebug(void) {
    s_stream.data = NULL;
    s_stream.ops = &OPS;
    s_totalcolumns = 80;
    s_totalrows = 25;
 
    s_chars = (void *)0xb8000;
    tty_setdebugconsole(&s_stream);
}

void archx86_vgatty_init(physptr baseaddr, size_t columns, size_t rows, size_t bytesperrow) {
    s_stream.data = NULL;
    s_stream.ops = &OPS;
    s_totalcolumns = columns;
    s_totalrows = rows;
    assert(columns * 2 == bytesperrow);
 
    s_chars = vmm_ezmap(baseaddr, s_totalcolumns * s_totalrows * 2);
    for (size_t r = 0; r < s_totalrows; r++) {
        for (size_t c = 0; c < s_totalcolumns; c++) {
            writecharat(r, c, ' ');
            writeattrat( r, c, 0x07);
        }
    }
    tty_setconsole(&s_stream);
}

