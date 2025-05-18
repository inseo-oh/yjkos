
#include <kernel/io/iodev.h>
#include <kernel/io/stream.h>
#include <kernel/io/tty.h>
#include <kernel/lib/diagnostics.h>

[[nodiscard]] int Tty_Register(struct Tty *out, void *data) {
    out->data = data;
    return Iodev_Register(&out->iodev, IODEV_TYPE_TTY, out);
}

struct Stream *Tty_GetStream(struct Tty *self) {
    return &self->stream;
}
