#include <assert.h>
#include <kernel/arch/interrupts.h>
#include <kernel/io/stream.h>
#include <kernel/status.h>
#include <kernel/ticktime.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static size_t measuredec_unsigned(uint32_t i) {
    size_t len = 0;
    unsigned long divisor = 1;
    {
        unsigned long current = i;
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

static size_t measuredec_signed(int64_t i) {
    size_t len = 0;
    if (i < 0) {
        len++;
        i = -i;
    }
    return len + measuredec_unsigned(i);
}

static FAILABLE_FUNCTION printdec_unsigned(struct stream *self, uint64_t i) {
FAILABLE_PROLOGUE
    unsigned long divisor = 1;
    {
        unsigned long current = i;
        while (current / 10) {
            divisor *= 10;
            current /= 10;
        }
    }
    for (; divisor; divisor /= 10) {
        int digit = (i / divisor) % 10;
        TRY(stream_putchar(self, '0' + digit));
    }
FAILABLE_EPILOGUE_BEGIN
FAILABLE_EPILOGUE_END
}

static FAILABLE_FUNCTION printdec_signed(struct stream *self, int64_t i) {
FAILABLE_PROLOGUE
    if (i < 0) {
        TRY(stream_putchar(self, '-'));
        i = -i;
    }
    TRY(printdec_unsigned(self, i));
FAILABLE_EPILOGUE_BEGIN
FAILABLE_EPILOGUE_END
}

static size_t measurehex(unsigned long i) {
    size_t len = 0;
    unsigned long divisor = 1;
    {
        unsigned long current = i;
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

static FAILABLE_FUNCTION printhex(struct stream *self, unsigned long i, bool uppercase) {
FAILABLE_PROLOGUE
    char a = uppercase ? 'A' : 'a';
    unsigned long divisor = 1;
    {
        unsigned long current = i;
        while (current / 16) {
            divisor *= 16;
            current /= 16;
        }
    }
    for (; divisor; divisor /= 16) {
        int digit = (i / divisor) % 16;
        if (digit < 10) {
            TRY(stream_putchar(self, '0' + digit));
        } else {
            TRY(stream_putchar(self, a + (digit - 10)));
        }
    }
FAILABLE_EPILOGUE_BEGIN
FAILABLE_EPILOGUE_END
}

FAILABLE_FUNCTION stream_putchar(struct stream *self, char c) {
FAILABLE_PROLOGUE
    TRY(self->ops->write(self, &c, 1));
FAILABLE_EPILOGUE_BEGIN
FAILABLE_EPILOGUE_END
}

FAILABLE_FUNCTION stream_putstr(struct stream *self, char const *s) {
FAILABLE_PROLOGUE
    for (char const *nextchar = s; *nextchar != '\0'; nextchar++) {
        TRY(stream_putchar(self, *nextchar));
    }
FAILABLE_EPILOGUE_BEGIN
FAILABLE_EPILOGUE_END
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

FAILABLE_FUNCTION stream_vprintf(struct stream *self, char const *fmt, va_list ap) {
FAILABLE_PROLOGUE
    uint8_t flags;
    char padchar;
    enum lenmod lenmod;
    uint32_t minwidth;
    size_t measureresult;

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
    TRY(stream_putchar(self, fmt[0]));
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
            TRY(stream_putchar(self, c));
            break;
        }
        case 's': {
            char const *s = va_arg(ap, char *);
            TRY(stream_putstr(self, s));
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
                    TRY(stream_putchar(self, padchar));
                }
            }
            TRY(printdec_signed(self, val));
            break;
        }
        case 'u': {
            uintmax_t val;
            switch(lenmod) {
                case LENMOD_CHAR:
                case LENMOD_SHORT:
                case LENMOD_INT:
                    val = va_arg(ap, unsigned int);
                    break;
                case LENMOD_LONG:
                    val = va_arg(ap, unsigned long);
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
                    TRY(stream_putchar(self, padchar));
                }
            }
            TRY(printdec_unsigned(self, val));
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
                    val = va_arg(ap, unsigned int);
                    break;
                case LENMOD_LONG:
                    val = va_arg(ap, unsigned long);
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
                TRY(stream_putstr(self, isuppercase ? "0X" : "0x"));
            }
            if (flags & FMTFLAG_MINWIDTH_PRESENT) {
                for (size_t i = measureresult; i < minwidth; i++) {
                    TRY(stream_putchar(self, padchar));
                }
            }
            TRY(printhex(self, val, isuppercase));
            break;
        }
        case 'p': {
            void *p = va_arg(ap, void *);
            TRY(stream_putstr(self, "0x"));
            TRY(printhex(self, (uintptr_t)p, false));
            break;
        }
        default:
            TRY(stream_putchar(self, fmt[0]));
            break;
    }
    fmt++;
    goto percentorchar;
end:
FAILABLE_EPILOGUE_BEGIN
FAILABLE_EPILOGUE_END
}

FAILABLE_FUNCTION stream_printf(struct stream *self, char const *fmt, ...) {
FAILABLE_PROLOGUE
    va_list ap;

    va_start(ap, fmt);
    TRY(stream_vprintf(self, fmt, ap));
    va_end(ap);
FAILABLE_EPILOGUE_BEGIN
FAILABLE_EPILOGUE_END
}

FAILABLE_FUNCTION stream_waitchar(char *char_out, struct stream *self, ticktime timeout) {
FAILABLE_PROLOGUE
    size_t size = 0;
    if (timeout != 0) {
        assert(arch_interrupts_areenabled());
    }

    ticktime starttime = g_ticktime;
    while (size < 1) {
        if ((timeout != 0) && (timeout <= (g_ticktime - starttime))) {
            THROW(ERR_EOF);
        }
        TRY(self->ops->read(&size, self, char_out, 1));
    }

FAILABLE_EPILOGUE_BEGIN
FAILABLE_EPILOGUE_END
}

FAILABLE_FUNCTION stream_getchar(char *char_out, struct stream *self) {
FAILABLE_PROLOGUE
    size_t size = 0;

    TRY(self->ops->read(&size, self, char_out, 1));
    THROW(ERR_EOF);

FAILABLE_EPILOGUE_BEGIN
FAILABLE_EPILOGUE_END
}
