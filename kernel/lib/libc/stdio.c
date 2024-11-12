#include <limits.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <kernel/io/co.h>
#include <kernel/io/stream.h>
#include <kernel/lib/diagnostics.h>
#include <sys/types.h>

// XXX: We have to bring rest of kernel stream API to libc as normal stdio API.

struct vsnprintf_stream {
    struct stream stream;
    char * restrict dest;
    size_t remaininglen;
};

static WARN_UNUSED_RESULT ssize_t vsnprintf_stream_op_write(
    struct stream *self, void *buf, size_t size)
{
    assert(size < STREAM_MAX_TRANSFER_SIZE);
    struct vsnprintf_stream *stream = self->data;
    size_t writelen = size;
    if (stream->remaininglen < writelen) {
        writelen = stream->remaininglen;
    }
    memcpy(stream->dest, buf, writelen);
    stream->dest[writelen] = '\0';
    stream->remaininglen -= writelen;
    stream->dest += writelen;
    return (ssize_t)writelen;
}

static const struct stream_ops VSNPRINTF_STREAM_OPS = {
    .write = vsnprintf_stream_op_write,
};


int vsnprintf(char * restrict str, size_t size, char const *fmt, va_list ap) {
    assert(size <= INT_MAX);
    struct vsnprintf_stream stream;
    stream.dest = str;
    stream.remaininglen = size - 1;
    stream.stream.ops = &VSNPRINTF_STREAM_OPS;
    stream.stream.data = &stream;
    return stream_vprintf(&stream.stream, fmt, ap);
}

int vsprintf(char * restrict str, char const *fmt, va_list ap) {
    return vsnprintf(str, SIZE_MAX, fmt, ap);
}

int snprintf(char * restrict str, size_t size, char const *fmt, ...) {
    assert(size <= INT_MAX);
    va_list ap;
    va_start(ap, fmt);
    int ret = vsnprintf(str, size, fmt, ap);
    va_end(ap);
    return ret;
}

int sprintf(char * restrict str, char const *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int ret = vsprintf(str, fmt, ap);
    va_end(ap);
    return ret;
}
