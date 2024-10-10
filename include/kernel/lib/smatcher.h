#pragma once
#include <stddef.h>
#include <stdbool.h>

typedef struct smatcher smatcher_t;
struct smatcher {
    char const *str;
    size_t len;
    size_t currentindex;
};

void smatcher_init(smatcher_t *out, char const *str);
void smatcher_init_with_len(smatcher_t *out, char const *str, size_t len);
void smatcher_slice(smatcher_t *out, smatcher_t const *self, size_t firstchar, size_t lastchar);
bool smatcher_consumestringifmatch(smatcher_t *self, char const *str);
// Note that while this only matches if given string is followed by whitespace, it will not
// consume the whitespace.
bool smatcher_consumewordifmatch(smatcher_t *self, char const *str);
void smatcher_skipwhitespaces(smatcher_t *self);
bool smatcher_consumeword(char const **str_out, size_t *len_out, smatcher_t *self);
