#pragma once
#include <kernel/lib/diagnostics.h>
#include <kernel/lib/list.h>
#include <kernel/ticktime.h>
#include <stdarg.h>
#include <stddef.h>
#include <sys/types.h>

#define STREAM_MAX_TRANSFER_SIZE 0x7fffffff
#define STREAM_EOF 0x100

struct Stream;
struct StreamOps {
    /*
     * Returns written length, or IOERROR_~ values on failure.
     */
    ssize_t (*Write)(struct Stream *self, void *buf, size_t size);
    /*
     * Returns written length, or IOERROR_~ values on failure.
     */
    ssize_t (*Read)(struct Stream *self, void *buf, size_t size);

    /* This is optional operation */
    void (*Flush)(struct Stream *self);
};

struct Stream {
    struct List_Node node;
    struct StreamOps const *ops;
    void *data;
};

[[nodiscard]] int Stream_PutChar(struct Stream *self, int c);
[[nodiscard]] ssize_t Stream_PutStr(struct Stream *self, char const *s);
[[nodiscard]] ssize_t Stream_VPrintf(struct Stream *self, char const *fmt, va_list ap);
[[nodiscard]] ssize_t Stream_Printf(struct Stream *self, char const *fmt, ...);
/*
 * Set timeout to 0 for no timeout(wait infinitely).
 *
 * Returns STREAM_EOF on timeout.
 */
int Stream_WaitChar(struct Stream *self, TICKTIME timeout);
/*
 * Returns STREAM_EOF on EOF.
 */
int Stream_GetChar(struct Stream *self);

void Stream_Flush(struct Stream *self);
