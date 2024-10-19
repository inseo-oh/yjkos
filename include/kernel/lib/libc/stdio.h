#ifndef _STDIO_H
#define _STDIO_H    1
#include <stddef.h>

#define __YJK_USE_INTERNAL
#include "_internal/stdarg_va_list.h"

// https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/stdio.h.html

#define SEEK_CUR        0
#define SEEK_END        1
#define SEEK_SET        2

int vsnprintf(char * restrict str, size_t size, char const *fmt, va_list ap);
int vsprintf(char * restrict str, char const *fmt, va_list ap);
int snprintf(char * restrict str, size_t size, char const *fmt, ...);
int sprintf(char * restrict str, char const *fmt, ...);

#endif
