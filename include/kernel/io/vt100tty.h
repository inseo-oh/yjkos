#pragma once
#include <stdbool.h>
#include <stddef.h>

struct vt100tty_screenline {
    char *chars;             // Each line must consist of the same number of characters.
    bool iscontinuation : 1; // Is this line continuation of the last line?
};

void vt100tty_init(struct vt100tty_screenline *screenlines, size_t columns, size_t rows, void (*updatescreen_op)(struct vt100tty_screenline *s_screenlines));
