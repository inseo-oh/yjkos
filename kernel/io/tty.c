
#include <kernel/io/iodev.h>
#include <kernel/io/stream.h>
#include <kernel/io/tty.h>
#include <kernel/lib/diagnostics.h>

NODISCARD int tty_register(struct tty *out, void *data) {
    out->data = data;
    return iodev_register(&out->iodev, IODEV_TYPE_TTY, out);
}

struct stream *tty_getstream(struct tty *self) {
    return &self->stream;
}
