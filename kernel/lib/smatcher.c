#include <assert.h>
#include <ctype.h>
#include <kernel/lib/smatcher.h>
#include <string.h>

void smatcher_init(struct smatcher *out, char const *str) {
    smatcher_init_with_len(out, str, strlen(str));
}

void smatcher_init_with_len(struct smatcher *out, char const *str, size_t len) {
    memset(out, 0, sizeof(*out));
    out->str = str;
    out->len = len;
}

void smatcher_slice(struct smatcher *out, struct smatcher const *self, size_t firstchar, size_t lastchar) {
    assert(firstchar <= lastchar);
    size_t len = lastchar - firstchar + 1;
    smatcher_init_with_len(out, &self->str[firstchar], len);
}

bool smatcher_consume_str_if_match(struct smatcher *self, char const *str) {
    size_t len = strlen(str);
    if ((self->len - self->currentindex) < len) {
        return false;
    }
    if (strncmp(&self->str[self->currentindex], str, len) != 0) {
        return false;
    }
    self->currentindex += len;
    return true;
}

bool smatcher_consume_word_if_match(struct smatcher *self, char const *str) {
    size_t len = strlen(str);
    if ((self->len - self->currentindex) < len) {
        return false;
    }
    if (strncmp(&self->str[self->currentindex], str, len) != 0) {
        return false;
    }
    char nextchar = self->str[self->currentindex + len];
    if (((self->currentindex + len) != self->len) && !isspace(nextchar)) {
        return false;
    }
    self->currentindex += len;
    return true;
}

void smatcher_skip_whitespaces(struct smatcher *self) {
    while (isspace(self->str[self->currentindex])) {
        self->currentindex++;
    }
}

bool smatcher_consume_word(char const **str_out, size_t *len_out, struct smatcher *self) {
    if ((self->currentindex == self->len) || isspace(self->str[self->currentindex])) {
        return false;
    }
    *str_out = &self->str[self->currentindex];
    *len_out = 0;
    for (; self->currentindex < self->len; self->currentindex++) {
        if (!isspace(self->str[self->currentindex])) {
            (*len_out)++;
        } else {
            break;
        }
    }
    return true;
}
