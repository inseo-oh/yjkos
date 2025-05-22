#include <kernel/arch/hcf.h>
#include <kernel/arch/interrupts.h>
#include <kernel/io/co.h>
#include <kernel/io/stream.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

static struct stream *s_primary_stream;
/* NOTE: Debug console is output only */
static struct stream *s_debug_stream;

void co_set_primary_console(struct stream *device) {
    s_primary_stream = device;
}

void co_set_debug_console(struct stream *device) {
    s_debug_stream = device;
}

void co_ask_primary_console(void) {
    if ((s_primary_stream != nullptr) && (s_debug_stream == nullptr)) {
        return;
    } else if ((s_primary_stream == nullptr) && (s_debug_stream != nullptr)) {
        s_primary_stream = s_debug_stream;
        s_debug_stream = nullptr;
        return;
    }
    int ret = stream_printf(s_primary_stream, "\n\nPress 1 to select this console.\n\n");
    (void)ret;
    ret = stream_printf(s_debug_stream, "\n\nPress 2 to select this console.\n\n");
    (void)ret;
    stream_flush(s_primary_stream);
    stream_flush(s_debug_stream);

    while (1) {
        ret = stream_wait_char(s_primary_stream, 10);
        if (ret == '1') {
            /* Use current primary console */
            return;
        }
        ret = stream_wait_char(s_debug_stream, 10);
        if (ret == '2') {
            /* Swap debug console with primary one */
            struct stream *temp = s_primary_stream;
            s_primary_stream = s_debug_stream;
            s_debug_stream = temp;
            return;
        }
    }
}

void co_put_char(char c) {
    bool prev_interrupts = arch_irq_disable();
    if (s_primary_stream != nullptr) {
        int ret = stream_put_char(s_primary_stream, c);
        (void)ret;
        stream_flush(s_primary_stream);
    }
    if (s_debug_stream != nullptr && (s_primary_stream != s_debug_stream)) {
        int ret = stream_put_char(s_debug_stream, c);
        (void)ret;
        stream_flush(s_debug_stream);
    }
    arch_irq_restore(prev_interrupts);
}

void co_put_string(char const *s) {
    bool prev_interrupts = arch_irq_disable();
    if (s_primary_stream != nullptr) {
        int ret = stream_put_string(s_primary_stream, s);
        (void)ret;
        stream_flush(s_primary_stream);
    }
    if (s_debug_stream != nullptr && (s_primary_stream != s_debug_stream)) {
        int ret = stream_put_string(s_debug_stream, s);
        (void)ret;
        stream_flush(s_debug_stream);
    }
    arch_irq_restore(prev_interrupts);
}

void co_vprintf(char const *fmt, va_list ap) {
    bool prev_interrupts = arch_irq_disable();
    if (s_primary_stream != nullptr) {
        int ret = stream_vprintf(s_primary_stream, fmt, ap);
        (void)ret;
        stream_flush(s_primary_stream);
    }
    if (s_debug_stream != nullptr && (s_primary_stream != s_debug_stream)) {
        int ret = stream_vprintf(s_debug_stream, fmt, ap);
        (void)ret;
        stream_flush(s_debug_stream);
    }
    arch_irq_restore(prev_interrupts);
}

void co_printf(char const *fmt, ...) {
    va_list ap;

    va_start(ap, fmt);
    co_vprintf(fmt, ap);
    va_end(ap);
}

int co_get_char(void) {
    int c;

    if (!s_primary_stream) {
        /* There's nothing we can do */
        co_printf("tty: waiting for character, but there's no console to wait for\n");
        arch_hcf();
        while (1) {
        }
    }
    int ret = -1;
    while (ret < 0) {
        ret = stream_wait_char(s_primary_stream, 0);
    }

    c = ret;
    if (c == '\r') {
        c = '\n';
    }
    return c;
}
