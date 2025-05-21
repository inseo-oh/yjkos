#pragma once
#include <stddef.h>

/* https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/string.h.html */

size_t strlen(char const *s);
int strcmp(char const *s1, char const *s2);
int strncmp(char const *s1, char const *s2, size_t n);
char *strchr(char const *s, int c);
char *strrchr(char const *s, int c);
void memset(void *s, int c, size_t n);
void memcpy(void *restrict dest, const void *restrict src, size_t n);
void memcpy32(void *restrict s1, const void *restrict s2, size_t n);

char *strdup(char const *s);

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

