#include <assert.h>
#include <kernel/arch/interrupts.h>
#include <kernel/io/stream.h>
#include <kernel/lib/diagnostics.h>
#include <kernel/ticktime.h>
#include <kernel/types.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

[[nodiscard]] static size_t measure_dec_unsigned(uint32_t i) {
    size_t len = 0;
    ULONG divisor = 1;
    {
        ULONG current = i;
        while (current / 10) {
            divisor *= 10;
            current /= 10;
        }
    }
    for (; divisor; divisor /= 10) {
        len++;
    }
    return len;
}

[[nodiscard]] static size_t measure_dec_signed(int64_t i) {
    size_t len = 0;
    if (i < 0) {
        len++;
        i = -i;
    }
    return len + measure_dec_unsigned(i);
}

[[nodiscard]] static ssize_t print_dec_unsigned(struct stream *self, uint64_t i) {
    size_t written_count = 0;
    ULONG divisor = 1;
    {
        ULONG current = i;
        while (current / 10) {
            divisor *= 10;
            current /= 10;
        }
    }
    for (; divisor; divisor /= 10) {
        int digit = (int)((i / divisor) % 10);
        int result = stream_put_char(self, '0' + digit);
        if (result < 0) {
            return result;
        }
        written_count++;
    }
    return (ssize_t)written_count;
}

[[nodiscard]] static ssize_t print_dec_signed(struct stream *self, int64_t i) {
    size_t written_count = 0;
    if (i < 0) {
        int result = stream_put_char(self, '-');
        if (result < 0) {
            return result;
        }
        written_count++;
        i = -i;
    }
    ssize_t result = print_dec_unsigned(self, i);
    if (result < 0) {
        return result;
    }
    written_count += result;
    return (ssize_t)written_count;
}

static size_t measurehex(ULONG i) {
    size_t len = 0;
    ULONG divisor = 1;
    {
        ULONG current = i;
        while (current / 16) {
            divisor *= 16;
            current /= 16;
        }
    }
    for (; divisor; divisor /= 16) {
        len++;
    }
    return len;
}

[[nodiscard]] static ssize_t print_hex(struct stream *self,
                                       uint64_t i, bool uppercase) {
    size_t written_count = 0;
    char a = uppercase ? 'A' : 'a';
    ULONG divisor = 1;
    {
        ULONG current = i;
        while (current / 16) {
            divisor *= 16;
            current /= 16;
        }
    }
    for (; divisor; divisor /= 16) {
        int digit = (int)((i / divisor) % 16);
        int result;
        if (digit < 10) {
            result = stream_put_char(self, '0' + digit);
        } else {
            result = stream_put_char(self, a + (digit - 10));
        }
        if (result < 0) {
            return result;
        }
        written_count++;
    }
    return (ssize_t)written_count;
}

[[nodiscard]] int stream_put_char(struct stream *self, int c) {
    ssize_t result = self->ops->write(self, &c, 1);
    if (result < 0) {
        return result;
    }
    assert(result != 0);
    return result;
}

[[nodiscard]] ssize_t stream_put_string(struct stream *self, char const *s) {
    if (!s) {
        s = "<null>";
    }
    size_t written_count = 0;
    for (char const *nextchar = s; *nextchar != '\0'; nextchar++, written_count++) {
        int result = stream_put_char(self, *nextchar);
        if (result < 0) {
            return result;
        }
    }
    return (ssize_t)written_count;
}

static uint8_t const FMTFLAG_ALTERNATEFORM = 1U << 0U;
static uint8_t const FMTFLAG_MINWIDTH_PRESENT = 1U << 1U;

typedef enum {
    LENMOD_INT,
    LENMOD_CHAR,
    LENMOD_SHORT,
    LENMOD_LONG,
    LENMOD_LONG_LONG,
    LENMOD_INTMAX,
    LENMOD_SIZE,
    LENMOD_PTRDIFF,
} LENMOD;

[[nodiscard]] ssize_t stream_vprintf(struct stream *self, char const *fmt, va_list ap) {
    uint8_t flags;
    char padchar;
    LENMOD lenmod;
    uint32_t minwidth;
    size_t measureresult;
    size_t written_count = 0;
    ssize_t ret;

percentorchar:
    if (!fmt[0]) {
        goto end;
    }
    if (fmt[0] == '%') {
        fmt++;
        lenmod = LENMOD_INT;
        flags = 0;
        minwidth = 0;
        padchar = ' ';
        goto fmtflag;
    }
    ret = stream_put_char(self, fmt[0]);
    if (ret < 0) {
        return ret;
    }
    written_count++;
    fmt++;
    goto percentorchar;
fmtflag:
    if (!fmt[0]) {
        goto end;
    }
    switch (fmt[0]) {
    case '#':
        fmt++;
        flags |= FMTFLAG_ALTERNATEFORM;
        goto fmtflag;
    case '0':
        fmt++;
        padchar = '0';
        goto fmtflag;
    /* TODO: -, <space>, +, */
    default:
        break;
    }
    goto fmtminwidth;
fmtminwidth:
    if (!fmt[0]) {
        goto end;
    }
    if (('0' <= fmt[0]) && (fmt[0] <= '9')) {
        flags |= FMTFLAG_MINWIDTH_PRESENT;
        minwidth = (minwidth * 10) + (fmt[0] - '0');
        ++fmt;
        goto fmtminwidth;
    }
    goto fmtprecision;
fmtprecision:
    if (!fmt[0]) {
        goto end;
    }
    goto fmtlenmod;
fmtlenmod:
    if (!fmt[0]) {
        goto end;
    }
    switch (fmt[0]) {
    case 'h':
        fmt++;
        if (fmt[0] == 'h') {
            /* hh */
            fmt++;
            lenmod = LENMOD_CHAR;
        } else {
            /* h */
            lenmod = LENMOD_SHORT;
        }
        break;
    case 'l':
        fmt++;
        if (fmt[0] == 'l') {
            /* ll */
            fmt++;
            lenmod = LENMOD_LONG_LONG;
        } else {
            /* l */
            lenmod = LENMOD_LONG;
        }
        break;
    case 'j':
        fmt++;
        lenmod = LENMOD_INTMAX;
        break;
    case 'z':
        fmt++;
        lenmod = LENMOD_SIZE;
        break;
    default:
        break;
    }
    goto doformat;
doformat:
    if (!fmt[0]) {
        goto end;
    }
    switch (fmt[0]) {
    case 'c': {
        char c = va_arg(ap, int);
        ret = stream_put_char(self, c);
        if (ret < 0) {
            return ret;
        }
        written_count++;
        break;
    }
    case 's': {
        char const *s = va_arg(ap, char *);
        ret = stream_put_string(self, s);
        if (ret < 0) {
            return ret;
        }
        written_count += ret;
        break;
    }
    case 'd': {
        intmax_t val;
        switch (lenmod) {
        case LENMOD_CHAR:
        case LENMOD_SHORT:
        case LENMOD_INT:
            val = va_arg(ap, int);
            break;
        case LENMOD_LONG:
            val = va_arg(ap, long);
            break;
        case LENMOD_LONG_LONG:
            val = va_arg(ap, long long);
            break;
        case LENMOD_SIZE:
            val = va_arg(ap, size_t);
            break;
        case LENMOD_INTMAX:
            val = va_arg(ap, uintmax_t);
            break;
        case LENMOD_PTRDIFF:
            val = va_arg(ap, ptrdiff_t);
            break;
        }
        if (flags & FMTFLAG_MINWIDTH_PRESENT) {
            measureresult = measure_dec_signed(val);
            for (size_t i = measureresult; i < minwidth; i++) {
                ret = stream_put_char(self, padchar);
                if (ret < 0) {
                    return ret;
                }
                written_count += ret;
            }
        }
        ret = print_dec_signed(self, val);
        if (ret < 0) {
            return ret;
        }
        written_count += ret;
        break;
    }
    case 'u': {
        uintmax_t val;
        switch (lenmod) {
        case LENMOD_CHAR:
        case LENMOD_SHORT:
        case LENMOD_INT:
            val = va_arg(ap, UINT);
            break;
        case LENMOD_LONG:
            val = va_arg(ap, ULONG);
            break;
        case LENMOD_LONG_LONG:
            val = va_arg(ap, unsigned long long);
            break;
        case LENMOD_SIZE:
            val = va_arg(ap, size_t);
            break;
        case LENMOD_INTMAX:
            val = va_arg(ap, uintmax_t);
            break;
        case LENMOD_PTRDIFF:
            val = va_arg(ap, ptrdiff_t);
            break;
        }
        if (flags & FMTFLAG_MINWIDTH_PRESENT) {
            measureresult = measure_dec_unsigned(val);
            for (size_t i = measureresult; i < minwidth; i++) {
                ret = stream_put_char(self, padchar);
                if (ret < 0) {
                    return ret;
                }
                written_count += ret;
            }
        }
        ret = print_dec_unsigned(self, val);
        if (ret < 0) {
            return ret;
        }
        written_count += ret;
        break;
    }
    case 'x':
    case 'X': {
        bool isuppercase = fmt[0] == 'X';
        uintmax_t val;
        switch (lenmod) {
        case LENMOD_CHAR:
        case LENMOD_SHORT:
        case LENMOD_INT:
            val = va_arg(ap, UINT);
            break;
        case LENMOD_LONG:
            val = va_arg(ap, ULONG);
            break;
        case LENMOD_LONG_LONG:
            val = va_arg(ap, unsigned long long);
            break;
        case LENMOD_SIZE:
            val = va_arg(ap, size_t);
            break;
        case LENMOD_INTMAX:
            val = va_arg(ap, uintmax_t);
            break;
        case LENMOD_PTRDIFF:
            val = va_arg(ap, ptrdiff_t);
            break;
        }
        measureresult = 0;
        if (flags & FMTFLAG_MINWIDTH_PRESENT) {
            measureresult = measurehex(val);
            if (flags & FMTFLAG_ALTERNATEFORM) {
                measureresult += 2;
            }
        }

        if (flags & FMTFLAG_ALTERNATEFORM) {
            ret = stream_put_string(self, isuppercase ? "0X" : "0x");
            if (ret < 0) {
                return ret;
            }
            written_count += ret;
        }
        if (flags & FMTFLAG_MINWIDTH_PRESENT) {
            for (size_t i = measureresult; i < minwidth; i++) {
                ret = stream_put_char(self, padchar);
                if (ret < 0) {
                    return ret;
                }
                written_count += ret;
            }
        }
        ret = print_hex(self, val, isuppercase);
        if (ret < 0) {
            return ret;
        }
        written_count += ret;
        break;
    }
    case 'p': {
        void *p = va_arg(ap, void *);
        ret = stream_put_string(self, "0x");
        if (ret < 0) {
            return ret;
        }
        written_count += ret;
        ret = print_hex(self, (uintptr_t)p, false);
        if (ret < 0) {
            return ret;
        }
        written_count += ret;
        break;
    }
    default:
        ret = stream_put_char(self, fmt[0]);
        if (ret < 0) {
            return ret;
        }
        written_count += ret;
        break;
    }
    fmt++;
    goto percentorchar;
end:
    return (ssize_t)written_count;
}

ssize_t stream_printf(struct stream *self, char const *fmt, ...) {
    va_list ap;

    va_start(ap, fmt);
    ssize_t result = stream_vprintf(self, fmt, ap);
    va_end(ap);
    return result;
}

int stream_wait_char(struct stream *self, TICKTIME timeout) {
    ssize_t size = 0;
    if (timeout != 0) {
        assert(arch_irq_are_enabled());
    }

    TICKTIME starttime = g_ticktime;
    uint8_t chr;
    while (1) {
        if ((timeout != 0) && (timeout <= (g_ticktime - starttime))) {
            return STREAM_EOF;
        }
        size = self->ops->read(self, &chr, 1);
        if (size < 0) {
            return size;
        }
        if (size != 0) {
            break;
        }
    }
    return chr;
}

int stream_get_char(struct stream *self) {
    uint8_t chr;
    ssize_t size = self->ops->read(self, &chr, 1);
    if (size == 0) {
        return STREAM_EOF;
    }
    return chr;
}

void stream_flush(struct stream *self) {
    if (self->ops->flush == NULL) {
        return;
    }
    self->ops->flush(self);
}
