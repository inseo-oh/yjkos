#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdint.h>

struct stream;

#define CON_BACKSPACE   '\x7f'
#define CON_DELETE      '\x08'

// For tty_~console functions, pass NULL to disable the console.
void tty_setconsole(struct stream *device);
void tty_setdebugconsole(struct stream *device);
void tty_putc(char c);
void tty_puts(char const *s);
void tty_vprintf(char const *fmt, va_list ap);
char tty_getchar(void);
void tty_printf(char const *format, ...);
