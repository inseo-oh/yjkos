#include <assert.h>
#include <ctype.h>
#include <kernel/lib/smatcher.h>
#include <string.h>

void Smatcher_Init(struct SMatcher *out, char const *str) {
    Smatcher_InitWithLen(out, str, strlen(str));
}

void Smatcher_InitWithLen(struct SMatcher *out, char const *str, size_t len) {
    memset(out, 0, sizeof(*out));
    out->str = str;
    out->len = len;
}

void Smatcher_Slice(struct SMatcher *out, struct SMatcher const *self, size_t firstchar, size_t lastchar) {
    assert(firstchar <= lastchar);
    size_t len = lastchar - firstchar + 1;
    Smatcher_InitWithLen(out, &self->str[firstchar], len);
}

bool Smatcher_ConsumeStrIfMatch(struct SMatcher *self, char const *str) {
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

bool Smatcher_ConsumeWordIfMatch(struct SMatcher *self, char const *str) {
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

void Smatcher_SkipWhitespaces(struct SMatcher *self) {
    while (isspace(self->str[self->currentindex])) {
        self->currentindex++;
    }
}

bool Smatcher_ConsumeWord(char const **str_out, size_t *len_out, struct SMatcher *self) {
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
