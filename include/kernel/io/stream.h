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
    ssize_t (*write)(struct stream *self, void *buf, size_t size);
    /*
     * Returns written length, or IOERROR_~ values on failure.
     */
    ssize_t (*read)(struct stream *self, void *buf, size_t size);

    /* This is optional operation */
    void (*flush)(struct stream *self);
};

struct stream {
    struct list_node node;
    struct stream_ops const *ops;
    void *data;
};

[[nodiscard]] int stream_put_char(struct stream *self, int c);
[[nodiscard]] ssize_t stream_put_string(struct stream *self, char const *s);
[[nodiscard]] ssize_t stream_vprintf(struct stream *self, char const *fmt, va_list ap);
[[nodiscard]] ssize_t stream_printf(struct stream *self, char const *fmt, ...);
/*
 * Set timeout to 0 for no timeout(wait infinitely).
 *
 * Returns STREAM_EOF on timeout.
 */
int stream_wait_char(struct stream *self, TICKTIME timeout);
/*
 * Returns STREAM_EOF on EOF.
 */
int stream_get_char(struct stream *self);

void stream_flush(struct stream *self);
