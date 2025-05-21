#include <kernel/lib/strutil.h>
#include <kernel/mem/heap.h>
#include <stddef.h>
#include <ctype.h>

/* https://pubs.opengroup.org/onlinepubs/9799919799/functions/strlen.html */
size_t strlen(char const *s) {
    size_t len = 0;
    for (char const *next = s; *next != '\0'; next++, len++) {
    }
    return len;
}

/* https://pubs.opengroup.org/onlinepubs/9799919799/functions/strcmp.html */
int strcmp(char const *s1, char const *s2) {
    for (size_t idx = 0;; idx++) {
        if (s1[idx] != s2[idx]) {
            return s1[idx] - s2[idx];
        }
        if (s1[idx] == 0) {
            break;
        }
    }
    return 0;
}

/* https://pubs.opengroup.org/onlinepubs/9799919799/functions/strncmp.html */
int strncmp(char const *s1, char const *s2, size_t n) {
    for (size_t idx = 0; idx < n; idx++) {
        if (s1[idx] != s2[idx]) {
            return s1[idx] - s2[idx];
        }
        if (s1[idx] == 0) {
            break;
        }
    }
    return 0;
}

/* https://pubs.opengroup.org/onlinepubs/9799919799/functions/strchr.html */
char *strchr(char const *s, int c) {
    for (char *next = (char *)s;; next++) {
        if (*next == c) {
            return next;
        }
        if (*next == '\0') {
            return NULL;
        }
    }
    return NULL;
}

/* https://pubs.opengroup.org/onlinepubs/9799919799/functions/strrchr.html */
char *strrchr(char const *s, int c) {
    char *result = NULL;
    for (char *next = (char *)s;; next++) {
        if (*next == c) {
            result = next;
        }
        if (*next == '\0') {
            break;
        }
    }
    return result;
}

/* https://pubs.opengroup.org/onlinepubs/9799919799/functions/memset.html */
void memset(void *s, int c, size_t n) {
    char *next = (char *)s;
    for (size_t i = 0; i < n; i++) {
        *(next++) = (char)c;
    }
}

/* https://pubs.opengroup.org/onlinepubs/9799919799/functions/memcpy.html */
void memcpy(void *restrict dest, const void *restrict src, size_t n) {
#ifdef YJKERNEL_ARCH_I586
    int dummy[3];
    __asm__ volatile(
        "pushf\n"
        "cld\n"
        "rep movsb\n"
        "popf\n"
        : "=c"(dummy[0]),
          "=S"(dummy[1]),
          "=D"(dummy[2])
        : "c"(n),
          "S"(src),
          "D"(dest));
#else
    for (size_t i = 0; i < n; i++) {
        ((char *)s1)[i] = ((char const *)s2)[i];
    }
#endif
}

/* https://pubs.opengroup.org/onlinepubs/9799919799/functions/strdup.html */
char *strdup(char const *s) {
    char *mem;
    size_t size = strlen(s) + 1;
    if (size == 0) {
        goto oom;
    }
    mem = (char *)heap_alloc(size, 0);
    if (mem == NULL) {
        goto oom;
    }
    memcpy(mem, s, size - 1);
    mem[size - 1] = '\0';
    return mem;
oom:
#ifndef YJKERNEL_SOURCE
    /*
     * We don't have global errno in kernel, so errno setup can only be done in userspace libc.
     */
    assert(!"TODO: Set errno value");
#endif
    return NULL;
}


void memcpy32(void *restrict s1, const void *restrict s2, size_t n) {
#ifdef YJKERNEL_ARCH_I586
    int dummy[3];
    __asm__ volatile (
        "pushf\n"
        "cld\n"
        "rep movsl\n"
        "popf\n"
        : "=c"(dummy[0]),
          "=S"(dummy[1]),
          "=D"(dummy[2])
        : "c"(n),
          "S"(s2),
          "D"(s1)
    );
#else
    #error TODO
#endif
}

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
