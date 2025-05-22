#pragma once

struct term;

[[nodiscard]] int term_create(struct term **term_out, char const *id);
void term_clear(struct term *term);
void term_clear_line(struct term *term);
void term_set_cursor_pos(struct term *term, int x, int y);
void term_get_cursor_pos(struct term *term, int *x_out, int *y_out);
