#pragma once
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

struct stream;

#define CON_BACKSPACE   '\x7f'
#define CON_DELETE      '\x08'

/* For Co~Set~Console functions, pass NULL to disable the console. */
void co_set_primary_console(struct stream *device);
void co_set_debug_console(struct stream *device);
void co_ask_primary_console(void);
void co_put_char(char c);
void co_put_string(char const *s);
void co_vprintf(char const *fmt, va_list ap);
int co_get_char(void);
void co_printf(char const *fmt, ...);
