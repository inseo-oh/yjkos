#include <kernel/arch/interrupts.h>
#include <kernel/io/stream.h>
#include <kernel/io/tty.h>
#include <kernel/lib/list.h>
#include <kernel/status.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

static stream_t *s_console;
// NOTE: Debug console is output only
static stream_t *s_debugconsole;

void tty_setconsole(stream_t *device) {
    s_console = device;
}

void tty_setdebugconsole(stream_t *device) {
    s_debugconsole = device;
}

void tty_putc(char c) {
    if (s_console != NULL) {
        status_t status = stream_putchar(s_console, c);
        (void)status;
    }
    if (s_debugconsole != NULL && (s_console != s_debugconsole)) {
        status_t status = stream_putchar(s_debugconsole, c);
        (void)status;
    }
}

void tty_puts(char const *s) {
    if (s_console != NULL) {
        status_t status = stream_putstr(s_console, s);
        (void)status;
    }
    if (s_debugconsole != NULL && (s_console != s_debugconsole)) {
        status_t status = stream_putstr(s_debugconsole, s);
        (void)status;
    }
}

void tty_vprintf(char const *fmt, va_list ap) {
    if (s_console != NULL) {
        status_t status = stream_vprintf(s_console, fmt, ap);
        (void)status;
    }
    if (s_debugconsole != NULL && (s_console != s_debugconsole)) {
        status_t status = stream_vprintf(s_debugconsole, fmt, ap);
        (void)status;
    }
}

void tty_printf(char const *fmt, ...) {
    va_list ap;

    va_start(ap, fmt);
    tty_vprintf(fmt, ap);
    va_end(ap);
}

char tty_getchar(void) {
    char c;

    if (!s_console) {
        // There's nothing we can do
        tty_printf("tty: waiting for character, but there's no console to wait for\n");
        while(1) {}
    }
    while(stream_waitchar(&c, s_console, 0) != OK) {}
    if (c == '\r') {
        c = '\n';
    }
    return c;
}

