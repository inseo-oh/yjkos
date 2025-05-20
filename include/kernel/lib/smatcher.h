#pragma once
#include <stddef.h>

struct smatcher {
    char const *str;
    size_t len;
    size_t currentindex;
};

void smatcher_init(struct smatcher *out, char const *str);
void smatcher_init_with_len(struct smatcher *out, char const *str, size_t len);
void smatcher_slice(struct smatcher *out, struct smatcher const *self, size_t firstchar, size_t lastchar);
bool smatcher_consume_str_if_match(struct smatcher *self, char const *str);
/* Note that while this only matches if given string is followed by whitespace, it will not consume the whitespace. */
bool smatcher_consume_word_if_match(struct smatcher *self, char const *str);
void smatcher_skip_whitespaces(struct smatcher *self);
bool smatcher_consume_word(char const **str_out, size_t *len_out, struct smatcher *self);
