#include <assert.h>
#include <kernel/arch/interrupts.h>
#include <kernel/io/stream.h>
#include <kernel/lib/diagnostics.h>
#include <kernel/ticktime.h>
#include <kernel/types.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

static WARN_UNUSED_RESULT size_t measuredec_unsigned(uint32_t i) {
    size_t len = 0;
    ulong divisor = 1;
    {
        ulong current = i;
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

static WARN_UNUSED_RESULT size_t measuredec_signed(int64_t i) {
    size_t len = 0;
    if (i < 0) {
        len++;
        i = -i;
    }
    return len + measuredec_unsigned(i);
}

static WARN_UNUSED_RESULT ssize_t printdec_unsigned(
    struct stream *self, uint64_t i) {
    size_t writtencount = 0;
    ulong divisor = 1;
    {
        ulong current = i;
        while (current / 10) {
            divisor *= 10;
            current /= 10;
        }
    }
    for (; divisor; divisor /= 10) {
        int digit = (i / divisor) % 10;
        int result = stream_putchar(self, '0' + digit);
        if (result < 0) {
            return result;
        }
        writtencount++;
    }
    return writtencount;
}

static WARN_UNUSED_RESULT ssize_t printdec_signed(
    struct stream *self, int64_t i) {
    size_t writtencount = 0;
    if (i < 0) {
        int result = stream_putchar(self, '-');
        if (result < 0) {
            return result;
        }
        writtencount++;
        i = -i;
    }
    ssize_t result = printdec_unsigned(self, i);
    if (result < 0) {
        return result;
    }
    writtencount += result;
    return writtencount;
}

static size_t measurehex(ulong i) {
    size_t len = 0;
    ulong divisor = 1;
    {
        ulong current = i;
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

static WARN_UNUSED_RESULT ssize_t printhex(struct stream *self,
    ulong i, bool uppercase
) {
    size_t writtencount = 0;
    char a = uppercase ? 'A' : 'a';
    ulong divisor = 1;
    {
        ulong current = i;
        while (current / 16) {
            divisor *= 16;
            current /= 16;
        }
    }
    for (; divisor; divisor /= 16) {
        int digit = (i / divisor) % 16;
        int result;
        if (digit < 10) {
            result = stream_putchar(self, '0' + digit);
        } else {
            result = stream_putchar(self, a + (digit - 10));
        }
        if (result < 0) {
            return result;
        }
        writtencount++;
    }
    return writtencount;
}

WARN_UNUSED_RESULT int stream_putchar(struct stream *self, char c) {
    ssize_t result = self->ops->write(self, &c, 1);
    if (result < 0) {
        return result;
    }
    assert(result != 0);
    return result;
}

WARN_UNUSED_RESULT ssize_t stream_putstr(struct stream *self, char const *s) {
    if (!s) {
        return stream_putstr(self, "<null>");
    }
    size_t writtencount = 0;
    for (
        char const *nextchar = s; *nextchar != '\0';
        nextchar++, writtencount++
    ) {
        int result = stream_putchar(self, *nextchar);
        if (result < 0) {
            return result;
        }
        writtencount++;
    }
    return writtencount;
}

static uint8_t const FMTFLAG_ALTERNATEFORM    = 1 << 0;
static uint8_t const FMTFLAG_MINWIDTH_PRESENT = 1 << 1;

enum lenmod {
    LENMOD_INT,
    LENMOD_CHAR,
    LENMOD_SHORT,
    LENMOD_LONG,
    LENMOD_LONG_LONG,
    LENMOD_INTMAX,
    LENMOD_SIZE,
    LENMOD_PTRDIFF,
};

WARN_UNUSED_RESULT ssize_t stream_vprintf(
    struct stream *self, char const *fmt, va_list ap
) {
    uint8_t flags;
    char padchar;
    enum lenmod lenmod;
    uint32_t minwidth;
    size_t measureresult;
    size_t writtencount = 0;
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
    ret = stream_putchar(self, fmt[0]);
    if (ret < 0) {
        return ret;
    }
    writtencount++;
    fmt++;
    goto percentorchar;
fmtflag:
    if (!fmt[0]) {
        goto end;
    }
    switch(fmt[0]) {
        case '#':
            fmt++;
            flags |= FMTFLAG_ALTERNATEFORM;
            goto fmtflag;
        case '0':
            fmt++;
            padchar = '0';
            goto fmtflag;
        // TODO: -, <space>, +, 
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
    switch(fmt[0]) {
        case 'h':
            fmt++;
            if (fmt[0] == 'h') {
                // hh
                fmt++;
                lenmod = LENMOD_CHAR;
            } else {
                // h
                lenmod = LENMOD_SHORT;
            }
            break;
        case 'l':
            fmt++;
            if (fmt[0] == 'l') {
                // ll
                fmt++;
                lenmod = LENMOD_LONG_LONG;
            } else {
                // l
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
    }
    goto doformat;
doformat:
    if (!fmt[0]) {
        goto end;
    }
    switch(fmt[0]) {
        case 'c': {
            char c = va_arg(ap, int);
            ret = stream_putchar(self, c);
            if (ret < 0) {
                return ret;
            }
            writtencount++;
            break;
        }
        case 's': {
            char const *s = va_arg(ap, char *);
            ret = stream_putstr(self, s);
            if (ret < 0) {
                return ret;
            }
            writtencount += ret;
            break;
        }
        case 'd': {
            intmax_t val;
            switch(lenmod) {
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
                measureresult = measuredec_signed(val);
                for (size_t i = measureresult; i < minwidth; i++) {
                    ret = stream_putchar(self, padchar);
                    if (ret < 0) {
                        return ret;
                    }
                    writtencount += ret;
                }
            }
            ret = printdec_signed(self, val);
            if (ret < 0) {
                return ret;
            }
            writtencount += ret;
            break;
        }
        case 'u': {
            uintmax_t val;
            switch(lenmod) {
                case LENMOD_CHAR:
                case LENMOD_SHORT:
                case LENMOD_INT:
                    val = va_arg(ap, uint);
                    break;
                case LENMOD_LONG:
                    val = va_arg(ap, ulong);
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
                measureresult = measuredec_unsigned(val);
                for (size_t i = measureresult; i < minwidth; i++) {
                    ret = stream_putchar(self, padchar);
                    if (ret < 0) {
                        return ret;
                    }
                    writtencount += ret;
                }
            }
            ret = printdec_unsigned(self, val);
            if (ret < 0) {
                return ret;
            }
            writtencount += ret;
            break;
        }
        case 'x':
        case 'X': {
            bool isuppercase = fmt[0] == 'X';
            uintmax_t val;
            switch(lenmod) {
                case LENMOD_CHAR:
                case LENMOD_SHORT:
                case LENMOD_INT:
                    val = va_arg(ap, uint);
                    break;
                case LENMOD_LONG:
                    val = va_arg(ap, ulong);
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
                ret = stream_putstr(self, isuppercase ? "0X" : "0x");
                if (ret < 0) {
                    return ret;
                }
                writtencount += ret;
            }
            if (flags & FMTFLAG_MINWIDTH_PRESENT) {
                for (size_t i = measureresult; i < minwidth; i++) {
                    ret = stream_putchar(self, padchar);
                    if (ret < 0) {
                        return ret;
                    }
                    writtencount += ret;
                }
            }
            ret = printhex(self, val, isuppercase);
            if (ret < 0) {
                return ret;
            }
            writtencount += ret;
            break;
        }
        case 'p': {
            void *p = va_arg(ap, void *);
            ret = stream_putstr(self, "0x");
            if (ret < 0) {
                return ret;
            }
            writtencount += ret;
            ret = printhex(self, (uintptr_t)p, false);
            if (ret < 0) {
                return ret;
            }
            writtencount += ret;
            break;
        }
        default:
            ret = stream_putchar(self, fmt[0]);
            if (ret < 0) {
                return ret;
            }
            writtencount += ret;
            break;
    }
    fmt++;
    goto percentorchar;
end:
    return writtencount;
}

ssize_t stream_printf(struct stream *self, char const *fmt, ...) {
    va_list ap;

    va_start(ap, fmt);
    ssize_t result = stream_vprintf(self, fmt, ap);
    va_end(ap);
    return result;
}

int stream_waitchar(struct stream *self, ticktime timeout) {
    ssize_t size = 0;
    if (timeout != 0) {
        assert(arch_interrupts_areenabled());
    }

    ticktime starttime = g_ticktime;
    uint8_t chr;
    while (1) {
        if ((timeout != 0) && (timeout <= (g_ticktime - starttime))) {
            return STREAM_EOF;
        }
        size = self->ops->read(self, &chr, 1);
        if (size < 0) {
            return size;
        } else if (size != 0) {
            break;
        }
    }
    return chr;
}

int stream_getchar(struct stream *self) {
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
