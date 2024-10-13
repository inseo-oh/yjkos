#include <kernel/arch/hcf.h>
#include <kernel/arch/interrupts.h>
#include <kernel/io/stream.h>
#include <kernel/io/tty.h>
#include <kernel/lib/list.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

static struct stream *s_console;
// NOTE: Debug console is output only
static struct stream *s_debugconsole;

void tty_setconsole(struct stream *device) {
    s_console = device;
}

void tty_setdebugconsole(struct stream *device) {
    s_debugconsole = device;
}

void tty_putc(char c) {
    if (s_console != NULL) {
        int ret = stream_putchar(s_console, c);
        (void)ret;
        stream_flush(s_console);
    }
    if (s_debugconsole != NULL && (s_console != s_debugconsole)) {
        int ret = stream_putchar(s_debugconsole, c);
        (void)ret;
        stream_flush(s_debugconsole);
    }
}

void tty_puts(char const *s) {
    if (s_console != NULL) {
        int ret = stream_putstr(s_console, s);
        (void)ret;
        stream_flush(s_console);
    }
    if (s_debugconsole != NULL && (s_console != s_debugconsole)) {
        int ret = stream_putstr(s_debugconsole, s);
        (void)ret;
        stream_flush(s_debugconsole);
    }
}

void tty_vprintf(char const *fmt, va_list ap) {
    if (s_console != NULL) {
        int ret = stream_vprintf(s_console, fmt, ap);
        (void)ret;
        stream_flush(s_console);
    }
    if (s_debugconsole != NULL && (s_console != s_debugconsole)) {
        int ret = stream_vprintf(s_debugconsole, fmt, ap);
        (void)ret;
        stream_flush(s_debugconsole);
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
        tty_printf(
            "tty: waiting for character, but there's no console to wait for\n");
        arch_hcf();
        while(1) {}
    }
    int ret = -1;
    while (ret < 0) {
        ret = stream_waitchar(s_console, 0);
    }

    c = ret;
    if (c == '\r') {
        c = '\n';
    }
    return c;
}

