#pragma once
#include <stddef.h>
#include <stdbool.h>

struct smatcher {
    char const *str;
    size_t len;
    size_t currentindex;
};

void smatcher_init(struct smatcher *out, char const *str);
void smatcher_init_with_len(struct smatcher *out, char const *str, size_t len);
void smatcher_slice(struct smatcher *out, struct smatcher const *self, size_t firstchar, size_t lastchar);
bool smatcher_consumestringifmatch(struct smatcher *self, char const *str);
// Note that while this only matches if given string is followed by whitespace, it will not
// consume the whitespace.
bool smatcher_consumewordifmatch(struct smatcher *self, char const *str);
void smatcher_skipwhitespaces(struct smatcher *self);
bool smatcher_consumeword(char const **str_out, size_t *len_out, struct smatcher *self);
