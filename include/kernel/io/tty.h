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

typedef enum LogLevel {
    LOGLEVEL_INFO  = 'I',
    LOGLEVEL_ERROR = 'E',
    LOGLEVEL_DEBUG = 'D',
} LogLevel;

void tty_printf(char const *format, ...);

#define Log(_lv, ...)    tty_printf("[LOG]"); tty_printf(__VA_ARGS__); tty_printf("\n");

#define CON_LOGI(...)           Log(LOGLEVEL_INFO, __VA_ARGS__)
#define CON_LOGE(...)           Log(LOGLEVEL_ERROR, __VA_ARGS__)
#define CON_LOGD(...)           Log(LOGLEVEL_DEBUG, __VA_ARGS__)

