#pragma once

struct term;

typedef enum {
    TERM_ATTR_BOLD = 1 << 0,
    TERM_ATTR_LOW_INTENSITY = 1 << 1,
    TERM_ATTR_UNDERLINE = 1 << 2,
    TERM_ATTR_BLINK = 1 << 3,
    TERM_ATTR_REVERSE = 1 << 4,
} TERM_ATTR;

[[nodiscard]] int term_create(struct term **term_out, char const *id, int left, int top, int width, int height);
struct kobject *term_get_object(struct term *term);
void term_clear(struct term *term);
void term_clear_line(struct term *term);
void term_set_cursor_pos(struct term *term, int x, int y);
void term_get_cursor_pos(struct term *term, int *x_out, int *y_out);
void term_set_attr(struct term *term, TERM_ATTR attr);
void term_clear_attr(struct term *term, TERM_ATTR attr);
void term_reset_attrs(struct term *term);
void term_write(struct term *term, char const *str);
