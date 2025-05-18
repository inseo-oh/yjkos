#pragma once
#include <kernel/io/iodev.h>
#include <kernel/io/stream.h>
#include <kernel/lib/diagnostics.h>

struct Tty {
    struct Stream stream;
    struct IoDev iodev;
    void *data;
};

[[nodiscard]] int Tty_Register(struct Tty *out, void *data);
struct Stream *Tty_GetStream(struct Tty *self);
