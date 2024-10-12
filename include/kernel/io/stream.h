#pragma once
#include <kernel/lib/list.h>
#include <kernel/status.h>
#include <kernel/ticktime.h>
#include <stddef.h>
#include <stdarg.h>

struct stream;
struct stream_ops {
    FAILABLE_FUNCTION (*write)(struct stream *self, void *buf, size_t size);
    FAILABLE_FUNCTION (*read)(size_t *size_out, struct stream *self, void *buf, size_t size);
};

struct stream {
    struct list_node node;
    struct stream_ops const *ops;
    void *data;
};

FAILABLE_FUNCTION stream_putchar(struct stream *self, char c);
FAILABLE_FUNCTION stream_putstr(struct stream *self, char const *s);
FAILABLE_FUNCTION stream_vprintf(struct stream *self, char const *fmt, va_list ap);
FAILABLE_FUNCTION stream_printf(struct stream *self, char const *fmt, ...);
// Set timeout to 0 for no timeout(wait infinitely)
FAILABLE_FUNCTION stream_waitchar(char *char_out, struct stream *self, ticktime_type timeout);


