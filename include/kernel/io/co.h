#pragma once
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct stream;

#define CON_BACKSPACE   '\x7f'
#define CON_DELETE      '\x08'

// For tty_~console functions, pass NULL to disable the console.
void co_setprimary(struct stream *device);
void co_setdebug(struct stream *device);
void co_putc(char c);
void co_puts(char const *s);
void co_vprintf(char const *fmt, va_list ap);
int co_getchar(void);
void co_printf(char const *fmt, ...);
