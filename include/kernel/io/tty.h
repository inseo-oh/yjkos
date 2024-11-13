#pragma once
#include <kernel/io/iodev.h>
#include <kernel/io/stream.h>
#include <kernel/lib/diagnostics.h>

struct tty {
    struct stream stream;
    struct iodev iodev;
    void *data;
};

WARN_UNUSED_RESULT int tty_register(struct tty *out, void *data);
struct stream *tty_getstream(struct tty *self);
