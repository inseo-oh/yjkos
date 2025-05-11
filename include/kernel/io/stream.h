#pragma once
#include <kernel/lib/diagnostics.h>
#include <kernel/lib/list.h>
#include <kernel/ticktime.h>
#include <stdarg.h>
#include <stddef.h>
#include <sys/types.h>

#define STREAM_MAX_TRANSFER_SIZE 0x7fffffff
#define STREAM_EOF 0x100

struct stream;
struct stream_ops {
    /*
     * Returns written length, or IOERROR_~ values on failure.
     */
    NODISCARD ssize_t (*write)(struct stream *self, void *buf, size_t size);
    /*
     * Returns written length, or IOERROR_~ values on failure.
     */
    NODISCARD ssize_t (*read)(struct stream *self, void *buf, size_t size);

    /* This is optional operation */
    void (*flush)(struct stream *self);
};

struct stream {
    struct list_node node;
    struct stream_ops const *ops;
    void *data;
};

NODISCARD int stream_putchar(struct stream *self, int c);
NODISCARD ssize_t stream_putstr(struct stream *self, char const *s);
NODISCARD ssize_t stream_vprintf(struct stream *self, char const *fmt, va_list ap);
NODISCARD ssize_t stream_printf(struct stream *self, char const *fmt, ...);
/*
 * Set timeout to 0 for no timeout(wait infinitely).
 *
 * Returns STREAM_EOF on timeout.
 */
int stream_waitchar(struct stream *self, TICKTIME timeout);
/*
 * Returns STREAM_EOF on EOF.
 */
int stream_getchar(struct stream *self);

void stream_flush(struct stream *self);
