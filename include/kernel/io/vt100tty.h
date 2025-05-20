#pragma once
#include <kernel/io/stream.h>
#include <stddef.h>

struct vt100tty_char {
    char chr;
    bool need_supdate : 1; /* Does this char need update? */
};

struct vt100tty_line_info {
    bool is_continuation : 1; /* Is this line continuation of the last line? */
    bool need_supdate : 1;    /* Does this line need update? */
};

struct vt100tty;

struct vt100tty_ops {
    /* NOTE: Callback must clear each character's `need_supdate` flag manually! */
    void (*update_screen)(struct vt100tty *self);

    /* Below is optional */
    void (*scroll)(struct vt100tty *self, int scrolllen);
};

/* TODO: Update to work with new TTY subsystem. */
struct vt100tty {
    struct stream stream;
    struct vt100tty_line_info *lineinfos;
    struct vt100tty_ops const *ops;
    struct vt100tty_char *chars;
    void *data;
    int columns, rows, currentcolumn, currentrow;
};

/* `chars` must hold `columns * rows` items at least. */
void vt100tty_init(struct vt100tty *out, struct vt100tty_line_info *lineinfos, struct vt100tty_char *chars, struct vt100tty_ops const *ops, int columns, int rows);
