#include <kernel/mem/heap.h>
#include <stddef.h>
#include <string.h>
#ifndef YJKERNEL_SOURCE
#include <assert.h>
#endif

// https://pubs.opengroup.org/onlinepubs/9799919799/functions/strlen.html
size_t strlen(char const *s) {
    size_t len = 0;
    for (char const *next = s; *next != '\0'; next++, len++) {
    }
    return len;
}

// https://pubs.opengroup.org/onlinepubs/9799919799/functions/strcmp.html
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

// https://pubs.opengroup.org/onlinepubs/9799919799/functions/strncmp.html
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

// https://pubs.opengroup.org/onlinepubs/9799919799/functions/strchr.html
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

// https://pubs.opengroup.org/onlinepubs/9799919799/functions/strrchr.html
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

// https://pubs.opengroup.org/onlinepubs/9799919799/functions/memset.html
void memset(void *s, int c, size_t n) {
    char *next = (char *)s;
    for (size_t i = 0; i < n; i++) {
        *(next++) = c;
    }
}

// https://pubs.opengroup.org/onlinepubs/9799919799/functions/memcpy.html
void memcpy(void *restrict s1, const void *restrict s2, size_t n) {
#ifdef YJKERNEL_ARCH_X86
    int dummy[3];
    __asm__ volatile (
        "pushf\n"
        "cld\n"
        "rep movsb\n"
        "popf\n"
        : "=c"(dummy[0]),
          "=S"(dummy[1]),
          "=D"(dummy[2])
        : "c"(n),
          "S"(s2),
          "D"(s1)
    );
#else
    for (size_t i = 0 ; i < n; i++) {
        ((char *)s1)[i] = ((char const *)s2)[i];
    }
#endif
}

// https://pubs.opengroup.org/onlinepubs/9799919799/functions/strdup.html
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
     * We don't have global errno in kernel, so errno setup can only be done in 
     * userspace libc.
     */
    // 
    assert(!"TODO: Set errno value");
#endif
    return NULL;
}
