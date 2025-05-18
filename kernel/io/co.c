#include <kernel/arch/hcf.h>
#include <kernel/arch/interrupts.h>
#include <kernel/io/co.h>
#include <kernel/io/stream.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

static struct Stream *s_primary_stream;
/* NOTE: Debug console is output only */
static struct Stream *s_debug_stream;

void Co_SetPrimaryConsole(struct Stream *device) {
    s_primary_stream = device;
}

void Co_SetDebugConsole(struct Stream *device) {
    s_debug_stream = device;
}

void Co_AskPrimaryConsole(void) {
    if ((s_primary_stream != NULL) && (s_debug_stream == NULL)) {
        return;
    } else if ((s_primary_stream == NULL) && (s_debug_stream != NULL)) {
        s_primary_stream = s_debug_stream;
        s_debug_stream = NULL;
        return;
    }
    int ret = Stream_Printf(s_primary_stream, "\n\nPress 1 to select this console.\n\n");
    (void)ret;
    ret = Stream_Printf(s_debug_stream, "\n\nPress 2 to select this console.\n\n");
    (void)ret;
    Stream_Flush(s_primary_stream);
    Stream_Flush(s_debug_stream);

    while (1) {
        ret = Stream_WaitChar(s_primary_stream, 10);
        if (ret == '1') {
            /* Use current primary console */
            return;
        }
        ret = Stream_WaitChar(s_debug_stream, 10);
        if (ret == '2') {
            /* Swap debug console with primary one */
            struct Stream *temp = s_primary_stream;
            s_primary_stream = s_debug_stream;
            s_debug_stream = temp;
            return;
        }
    }
}

void Co_PutChar(char c) {
    bool prev_interrupts = Arch_Irq_Disable();
    if (s_primary_stream != NULL) {
        int ret = Stream_PutChar(s_primary_stream, c);
        (void)ret;
        Stream_Flush(s_primary_stream);
    }
    if (s_debug_stream != NULL && (s_primary_stream != s_debug_stream)) {
        int ret = Stream_PutChar(s_debug_stream, c);
        (void)ret;
        Stream_Flush(s_debug_stream);
    }
    Arch_Irq_Restore(prev_interrupts);
}

void Co_PutString(char const *s) {
    bool prev_interrupts = Arch_Irq_Disable();
    if (s_primary_stream != NULL) {
        int ret = Stream_PutStr(s_primary_stream, s);
        (void)ret;
        Stream_Flush(s_primary_stream);
    }
    if (s_debug_stream != NULL && (s_primary_stream != s_debug_stream)) {
        int ret = Stream_PutStr(s_debug_stream, s);
        (void)ret;
        Stream_Flush(s_debug_stream);
    }
    Arch_Irq_Restore(prev_interrupts);
}

void Co_VPrintf(char const *fmt, va_list ap) {
    bool prev_interrupts = Arch_Irq_Disable();
    if (s_primary_stream != NULL) {
        int ret = Stream_VPrintf(s_primary_stream, fmt, ap);
        (void)ret;
        Stream_Flush(s_primary_stream);
    }
    if (s_debug_stream != NULL && (s_primary_stream != s_debug_stream)) {
        int ret = Stream_VPrintf(s_debug_stream, fmt, ap);
        (void)ret;
        Stream_Flush(s_debug_stream);
    }
    Arch_Irq_Restore(prev_interrupts);
}

void Co_Printf(char const *fmt, ...) {
    va_list ap;

    va_start(ap, fmt);
    Co_VPrintf(fmt, ap);
    va_end(ap);
}

int Co_GetChar(void) {
    int c;

    if (!s_primary_stream) {
        /* There's nothing we can do */
        Co_Printf("tty: waiting for character, but there's no console to wait for\n");
        Arch_Hcf();
        while (1) {
        }
    }
    int ret = -1;
    while (ret < 0) {
        ret = Stream_WaitChar(s_primary_stream, 0);
    }

    c = ret;
    if (c == '\r') {
        c = '\n';
    }
    return c;
}
