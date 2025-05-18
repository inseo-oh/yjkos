#pragma once
#include <kernel/io/stream.h>
#include <stddef.h>

struct Vt100Tty_Char {
    char chr;
    bool need_supdate : 1; /* Does this char need update? */
};

struct Vt100Tty_LineInfo {
    bool is_continuation : 1; /* Is this line continuation of the last line? */
    bool need_supdate : 1;    /* Does this line need update? */
};

struct Vt100Tty;

struct Vt100TtyOps {
    /* NOTE: Callback must clear each character's `need_supdate` flag manually! */
    void (*UpdateScreen)(struct Vt100Tty *self);

    /* Below is optional */
    void (*Scroll)(struct Vt100Tty *self, int scrolllen);
};

/* TODO: Update to work with new TTY subsystem. */
struct Vt100Tty {
    struct Stream stream;
    struct Vt100Tty_LineInfo *lineinfos;
    struct Vt100TtyOps const *ops;
    struct Vt100Tty_Char *chars;
    void *data;
    int columns, rows, currentcolumn, currentrow;
};

/* `chars` must hold `columns * rows` items at least. */
void Vt100tty_Init(struct Vt100Tty *out, struct Vt100Tty_LineInfo *lineinfos, struct Vt100Tty_Char *chars, struct Vt100TtyOps const *ops, int columns, int rows);
