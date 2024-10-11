#pragma once
#include <stddef.h> // IWYU pragma: export

// https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/string.h.html

size_t strlen(char const *s);
int strcmp(char const *s1, char const *s2);
int strncmp(char const *s1, char const *s2, size_t n);
char *strchr(char const *s, int c);
char *strrchr(char const *s, int c);
void memset(void *s, int c, size_t n);
void memcpy(void *restrict dest, const void *restrict src, size_t n);

char *strdup(char const *s);

//------------------------------------------------------------------------------
// FAILABLE_FUNCTION version of C API for use in kernel, which integrates well
// with kernel's error handling system.
//------------------------------------------------------------------------------

#ifdef YJKERNEL_SOURCE
#include <kernel/status.h>

FAILABLE_FUNCTION kstrdup(char **out, char const *s);
#endif
