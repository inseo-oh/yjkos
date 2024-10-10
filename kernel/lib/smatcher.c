#include <assert.h>
#include <ctype.h>
#include <kernel/lib/smatcher.h>
#include <stdbool.h>
#include <string.h>

void smatcher_init(smatcher_t *out, char const *str) {
    smatcher_init_with_len(out, str, strlen(str));
}

void smatcher_init_with_len(smatcher_t *out, char const *str, size_t len) {
    memset(out, 0, sizeof(*out));
    out->str = str;
    out->len = len;
}

void smatcher_slice(smatcher_t *out, smatcher_t const *self, size_t firstchar, size_t lastchar) {
    assert(firstchar <= lastchar);
    size_t len = lastchar - firstchar + 1;
    smatcher_init_with_len(out, &self->str[firstchar], len);
}

bool smatcher_consumestringifmatch(smatcher_t *self, char const *str) {
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

bool smatcher_consumewordifmatch(smatcher_t *self, char const *str) {
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

void smatcher_skipwhitespaces(smatcher_t *self) {
    while(isspace(self->str[self->currentindex])) {
        self->currentindex++;
    }
}

bool smatcher_consumeword(char const **str_out, size_t *len_out, smatcher_t *self) {
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
