#pragma once
#include <kernel/lib/list.h>
#include <kernel/status.h>
#include <kernel/ticktime.h>
#include <stddef.h>
#include <stdarg.h>

typedef struct stream stream_t;
typedef struct stream_ops stream_ops_t;
struct stream_ops {
    FAILABLE_FUNCTION (*write)(stream_t *self, void *buf, size_t size);
    FAILABLE_FUNCTION (*read)(size_t *size_out, stream_t *self, void *buf, size_t size);
};

struct stream {
    list_node_t node;
    stream_ops_t const *ops;
    void *data;
};

FAILABLE_FUNCTION stream_putchar(stream_t *self, char c);
FAILABLE_FUNCTION stream_putstr(stream_t *self, char const *s);
FAILABLE_FUNCTION stream_vprintf(stream_t *self, char const *fmt, va_list ap);
FAILABLE_FUNCTION stream_printf(stream_t *self, char const *fmt, ...);
// Set timeout to 0 for no timeout(wait infinitely)
FAILABLE_FUNCTION stream_waitchar(char *char_out, stream_t *self, ticktime_t timeout);


