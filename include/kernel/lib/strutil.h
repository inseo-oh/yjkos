#pragma once
#include <stddef.h>

/* https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/string.h.html */

size_t kstrlen(char const *s);
int kstrcmp(char const *s1, char const *s2);
int kstrncmp(char const *s1, char const *s2, size_t n);
char *kstrchr(char const *s, int c);
char *kstrrchr(char const *s, int c);
void vmemset(void *s, int c, size_t n);
void vmemcpy(void *restrict dest, const void *restrict src, size_t n);
void vmemcpy32(void *restrict s1, const void *restrict s2, size_t n);

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
