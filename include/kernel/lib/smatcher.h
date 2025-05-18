#pragma once
#include <stddef.h>

struct SMatcher {
    char const *str;
    size_t len;
    size_t currentindex;
};

void Smatcher_Init(struct SMatcher *out, char const *str);
void Smatcher_InitWithLen(struct SMatcher *out, char const *str, size_t len);
void Smatcher_Slice(struct SMatcher *out, struct SMatcher const *self, size_t firstchar, size_t lastchar);
bool Smatcher_ConsumeStrIfMatch(struct SMatcher *self, char const *str);
/* Note that while this only matches if given string is followed by whitespace, it will not consume the whitespace. */
bool Smatcher_ConsumeWordIfMatch(struct SMatcher *self, char const *str);
void Smatcher_SkipWhitespaces(struct SMatcher *self);
bool Smatcher_ConsumeWord(char const **str_out, size_t *len_out, struct SMatcher *self);
