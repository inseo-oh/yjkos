#include <kernel/arch/hcf.h>
#include <kernel/arch/interrupts.h>
#include <kernel/io/co.h>
#include <kernel/io/stream.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

static struct stream *s_primarystream;
// NOTE: Debug console is output only
static struct stream *s_debugstream;

void co_setprimary(struct stream *device) {
    s_primarystream = device;
}

void co_setdebug(struct stream *device) {
    s_debugstream = device;
}

void co_putc(char c) {
    bool previnterrupts =  arch_interrupts_disable();
    if (s_primarystream != NULL) {
        int ret = stream_put_char(s_primarystream, c);
        (void)ret;
        stream_flush(s_primarystream);
    }
    if (s_debugstream != NULL && (s_primarystream != s_debugstream)) {
        int ret = stream_put_char(s_debugstream, c);
        (void)ret;
        stream_flush(s_debugstream);
    }
    interrupts_restore(previnterrupts);
}

void co_puts(char const *s) {
    bool previnterrupts =  arch_interrupts_disable();
    if (s_primarystream != NULL) {
        int ret = stream_putstr(s_primarystream, s);
        (void)ret;
        stream_flush(s_primarystream);
    }
    if (s_debugstream != NULL && (s_primarystream != s_debugstream)) {
        int ret = stream_putstr(s_debugstream, s);
        (void)ret;
        stream_flush(s_debugstream);
    }
    interrupts_restore(previnterrupts);
}

void co_vprintf(char const *fmt, va_list ap) {
    bool previnterrupts =  arch_interrupts_disable();
    if (s_primarystream != NULL) {
        int ret = stream_vprintf(s_primarystream, fmt, ap);
        (void)ret;
        stream_flush(s_primarystream);
    }
    if (s_debugstream != NULL && (s_primarystream != s_debugstream)) {
        int ret = stream_vprintf(s_debugstream, fmt, ap);
        (void)ret;
        stream_flush(s_debugstream);
    }
    interrupts_restore(previnterrupts);
}

void co_printf(char const *fmt, ...) {
    va_list ap;

    va_start(ap, fmt);
    co_vprintf(fmt, ap);
    va_end(ap);
}

int co_getchar(void) {
    int c;

    if (!s_primarystream) {
        // There's nothing we can do
        co_printf(
            "tty: waiting for character, but there's no console to wait for\n");
        arch_hcf();
        while(1) {}
    }
    int ret = -1;
    while (ret < 0) {
        ret = stream_waitchar(s_primarystream, 0);
    }

    c = ret;
    if (c == '\r') {
        c = '\n';
    }
    return c;
}

