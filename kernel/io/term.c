#include "kernel/mem/heap.h"
#include <assert.h>
#include <kernel/io/co.h>
#include <kernel/io/term.h>
#include <kernel/kobject.h>
#include <kernel/panic.h>

#define KOBJECT_TYPE_TERM KOBJECT_MAKE_TYPE_CODE('t', 'e', 'r', 'm')

static struct kobject *s_root_obj;

struct char_data {
    char chr;
    TERM_ATTR attrs;
};

struct term {
    struct kobject *obj;
    struct char_data *chars;
    TERM_ATTR attrs;
    int left;
    int top;
    int width;
    int height;
    int cursor_x;
    int cursor_y;
    bool visible : 1;
};

static const struct kobject_ops TERM_KOBJECT_OPS = {

};

static struct term *get_term(struct kobject *obj) {
    if (!kobject_check_type(obj, KOBJECT_TYPE_TERM)) {
        panic("term: incorrect object type");
    }
    struct term *term = kobject_get_data(obj);
    assert(term->obj == obj);
    return term;
}
static struct term *get_parent_term(struct term *term) {
    return get_term(kobject_get_parent(term->obj));
}
static bool is_root_term(struct term *term) {
    return kobject_get_parent(term->obj) == s_root_obj;
}

static void set_char(struct term *term, struct char_data *chr_data, char chr) {
    chr_data->chr = chr;
    chr_data->attrs = term->attrs;
}
static void apply_attrs(struct term *term, TERM_ATTR attrs) {
    if (!term->visible) {
        return;
    }
    if (!is_root_term(term)) {
        struct term *p_term = get_parent_term(term);
        apply_attrs(p_term, attrs);
    } else {
        co_printf("\x1b[0%s%s%s%s%sm",
                  ((attrs & TERM_ATTR_BOLD) != 0) ? ";1" : "",
                  ((attrs & TERM_ATTR_LOW_INTENSITY) != 0) ? ";2" : "",
                  ((attrs & TERM_ATTR_UNDERLINE) != 0) ? ";4" : "",
                  ((attrs & TERM_ATTR_BLINK) != 0) ? ";5" : "",
                  ((attrs & TERM_ATTR_REVERSE) != 0) ? ";7" : "");
    }
}

[[nodiscard]] int term_create(struct term **term_out, char const *id, int left, int top, int width, int height) {
    struct kobject *obj = nullptr;
    struct term *term = nullptr;
    int ret;

    if (s_root_obj == nullptr) {
        ret = kobject_create(&s_root_obj, KOBJECT_TYPE_GENERIC, "term", sizeof(**term_out), &KOBJECT_OPS_EMPTY);
        if (ret < 0) {
            goto fail;
        }
    }
    ret = kobject_create(&obj, KOBJECT_TYPE_TERM, id, sizeof(**term_out), &TERM_KOBJECT_OPS);
    if (ret < 0) {
        goto fail;
    }
    ret = kobject_set_parent(obj, s_root_obj);
    if (ret < 0) {
        goto fail;
    }
    term = kobject_get_data(obj);
    *term_out = term;
    term->obj = obj;
    term->left = left;
    term->top = top;
    term->width = width;
    term->height = height;
    term->cursor_x = 0;
    term->cursor_y = 0;
    term->chars = heap_calloc(sizeof(*term->chars), (size_t)(width * height), HEAP_FLAG_ZEROMEMORY);
    term->visible = true;
    if (term->chars == nullptr) {
        goto fail;
    }
    ret = 0;
    goto out;
fail:
    if (term != nullptr) {
        heap_free(term->chars);
    }
    kobject_unref(obj);
out:
    return ret;
}
struct kobject *term_get_object(struct term *term) {
    return term->obj;
}
void term_clear(struct term *term) {
    int size = term->width * term->height;
    for (int i = 0; i < size; i++) {
        set_char(term, &term->chars[i], ' ');
    }
    if (!term->visible) {
        return;
    }
    if (!is_root_term(term)) {
        int old_x, old_y;
        struct term *p_term = get_parent_term(term);
        term_get_cursor_pos(p_term, &old_x, &old_y);

        for (int i = 0; i < term->height; i++) {
            term_set_cursor_pos(p_term, term->left, term->top + i);
            for (int j = 0; j < term->width; j++) {
                term_write(p_term, " ");
            }
        }

        term_set_cursor_pos(p_term, old_x, old_y);
    } else {
        co_printf("\x1b[2J");
    }
}
void term_clear_line(struct term *term) {
    if (!term->visible) {
        return;
    }
    int size = term->width;
    for (int i = 0; i < size; i++) {
        set_char(term, &term->chars[(term->cursor_y * term->width) + i], ' ');
    }
    if (!is_root_term(term)) {
        struct term *p_term = get_parent_term(term);
        int old_x, old_y;
        TERM_ATTR old_attrs = p_term->attrs;
        term_get_cursor_pos(p_term, &old_x, &old_y);
        term_set_cursor_pos(term, 0, term->cursor_y);
        p_term->attrs = term->attrs;
        for (int i = 0; i < term->width; i++) {
            term_write(p_term, " ");
        }
        p_term->attrs = old_attrs;
        term_set_cursor_pos(p_term, old_x, old_y);
        return;
    } else {
        apply_attrs(term, term->attrs);
        co_printf("\x1b[2K");
    }
}
void term_set_cursor_pos(struct term *term, int x, int y) {
    /* NOTE: x == width is allowed -- writing will cause immediate line break */
    if ((x < 0) || (term->width < x)) {
        x = term->cursor_x;
    }
    if ((y < 0) || (term->height <= y)) {
        y = term->cursor_y;
    }
    term->cursor_x = x;
    term->cursor_y = y;
    if (!term->visible) {
        return;
    }
    if (!is_root_term(term)) {
        struct term *p_term = get_parent_term(term);
        term_set_cursor_pos(p_term, term->left + x, term->top + y);
    } else {
        co_printf("\x1b[%d;%dH", y + 1, x + 1);
    }
}
void term_get_cursor_pos(struct term *term, int *x_out, int *y_out) {
    int row = 0;
    int col = 0;
    if (!is_root_term(term)) {
        *x_out = term->cursor_x;
        *y_out = term->cursor_y;
        return;
    }
    /* Send command ***********************************************************/
    co_printf("\x1b[6n");
    /* Wait \x1b[ *************************************************************/
    while (co_get_char() != '\x1b') {
    }
    if (co_get_char() != '[') {
        goto unknown;
    }
    /* Read row ***************************************************************/
    while (1) {
        char ch = co_get_char();
        if (ch == ';') {
            break;
        }
        if ((ch < '0') || ('9' < ch)) {
            goto unknown;
        }
        row *= 10;
        row += ch - '0';
    }
    /* Read column ************************************************************/
    while (1) {
        char ch = co_get_char();
        if (ch == 'R') {
            break;
        }
        if ((ch < '0') || ('9' < ch)) {
            goto unknown;
        }
        col *= 10;
        col += ch - '0';
    }
    goto out;
unknown:
    row = 0;
    col = 0;
out:
    *x_out = col;
    *y_out = row;
}
void term_set_attr(struct term *term, TERM_ATTR attr) {
    term->attrs |= attr;
}
void term_clear_attr(struct term *term, TERM_ATTR attr) {
    term->attrs &= ~attr;
}
void term_reset_attrs(struct term *term) {
    term->attrs = 0;
}
void term_write(struct term *term, char const *str) {
    int line_start_cursor_x = term->cursor_x;
    char const *remaining_lines_str = str;

    char const *next_chr = remaining_lines_str;
    struct char_data *dest = &term->chars[term->cursor_y * term->width + term->cursor_x];
    while (*next_chr != '\0') {
        if ((term->width <= term->cursor_x) || (*next_chr == '\n') || (*next_chr == '\r')) {
            size_t written_len = next_chr - remaining_lines_str;
            term_set_cursor_pos(term, line_start_cursor_x, term->cursor_y);
            if (!is_root_term(term)) {
                struct term *p_term = get_parent_term(term);
                TERM_ATTR old_attrs = p_term->attrs;
                p_term->attrs = term->attrs;
                for (size_t i = 0; i < written_len; i++) {
                    char str[] = {remaining_lines_str[i], 0};
                    term_write(p_term, str);
                }
                p_term->attrs = old_attrs;
            } else {
                apply_attrs(term, term->attrs);
                for (size_t i = 0; i < written_len; i++) {
                    co_put_char(remaining_lines_str[i]);
                    term->cursor_x++;
                }
            }
            remaining_lines_str = next_chr;
            line_start_cursor_x = 0;
            term->cursor_x = 0;
            if (*next_chr != '\r') {
                term->cursor_y++;
                while (term->height <= term->cursor_y) {
                    panic("term_write: TODO: scroll");
                }
            }
            dest = &term->chars[term->cursor_y * term->width + term->cursor_x];
            if ((*next_chr == '\n') || (*next_chr == '\r')) {
                next_chr++;
                remaining_lines_str++;
                continue;
            }
        }

        set_char(term, dest, *next_chr);
        next_chr++;
        dest++;
        term->cursor_x++;
    }
    int old_x = term->cursor_x;
    term_set_cursor_pos(term, line_start_cursor_x, term->cursor_y);
    if (!is_root_term(term)) {
        struct term *p_term = get_parent_term(term);
        TERM_ATTR old_attrs = p_term->attrs;
        p_term->attrs = term->attrs;
        term_write(p_term, remaining_lines_str);
        p_term->attrs = old_attrs;
    } else {
        apply_attrs(term, term->attrs);
        co_put_string(remaining_lines_str);
    }
    term_set_cursor_pos(term, old_x, term->cursor_y);
}
