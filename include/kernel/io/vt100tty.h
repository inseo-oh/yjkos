#pragma once
#include <stdbool.h>
#include <stddef.h>

typedef struct vt100tty_screenline vt100tty_screenline_t;
struct vt100tty_screenline {
    char *chars;             // Each line must consist of the same number of characters.
    bool iscontinuation : 1; // Is this line continuation of the last line?
};

void vt100tty_init(vt100tty_screenline_t *screenlines, size_t columns, size_t rows, void (*updatescreen_op)(vt100tty_screenline_t *s_screenlines));
