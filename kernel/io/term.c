#include <assert.h>
#include <kernel/io/co.h>
#include <kernel/io/term.h>
#include <kernel/kobject.h>
#include <kernel/panic.h>

#define KOBJECT_TYPE_TERM KOBJECT_MAKE_TYPE_CODE('t', 'e', 'r', 'm')

static struct kobject *s_root_obj;

struct term {
    struct kobject *obj;
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
static bool is_root_term(struct term *term) {
    return kobject_get_parent(term->obj) == s_root_obj;
}

[[nodiscard]] int term_create(struct term **term_out, char const *id) {
    struct kobject *obj = nullptr;
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
    *term_out = kobject_get_data(obj);
    (*term_out)->obj = obj;
    ret = 0;
    goto out;
fail:
    kobject_unref(obj);
out:
    return ret;
}
void term_clear(struct term *term) {
    if (!is_root_term(term)) {
        panic("term_clear: TODO - non-root terminal");
    }
    co_printf("\x1b[2J");
}
void term_clear_line(struct term *term) {
    if (!is_root_term(term)) {
        panic("term_clear_line: TODO - non-root terminal");
    }
    co_printf("\x1b[2K");
}
void term_set_cursor_pos(struct term *term, int x, int y) {
    if (!is_root_term(term)) {
        panic("term_set_cursor_pos: TODO - non-root terminal");
    }
    co_printf("\x1b[%d;%dH", x + 1, y + 1);
}
void term_get_cursor_pos(struct term *term, int *x_out, int *y_out) {
    int row = 0;
    int col = 0;
    if (!is_root_term(term)) {
        panic("term_get_cursor_pos: TODO - non-root terminal");
    }
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
