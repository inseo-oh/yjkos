#pragma once
#include <kernel/io/stream.h>
#include <stdbool.h>
#include <stddef.h>

struct vt100tty_char {
    char chr;
    bool needsupdate    : 1; // Does this char need update?
};

struct vt100tty_lineinfo {
    bool iscontinuation : 1; // Is this line continuation of the last line?
    bool needsupdate    : 1; // Does this line need update?
};

struct vt100tty;

struct vt100tty_ops {
    // NOTE: Callback must clear each character's `needsupdate` flag manually!
    void (*updatescreen)(struct vt100tty *self);

    // Below is optional
    void (*scroll)(struct vt100tty *self, int scrolllen);
};

struct vt100tty {
    struct stream stream;
    struct vt100tty_lineinfo *lineinfos;
    struct vt100tty_ops const *ops;
    struct vt100tty_char *chars;
    void *data;
    int columns, rows, currentcolumn, currentrow;
};


// `chars` must hold `columns * rows` items at least.
void vt100tty_init(
    struct vt100tty *out, struct vt100tty_lineinfo *lineinfos,
    struct vt100tty_char *chars, struct vt100tty_ops const *ops,
    int columns, int rows);
