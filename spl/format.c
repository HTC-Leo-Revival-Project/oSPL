/*
 * format.c - the one printf.
 *
 * There are three things to print to - the console, the UART, and both at
 * once when the bootloader is running - and each of them used to carry its
 * own copy of the same conversion loop.  This is that loop, once: the sinks
 * are a callback, so a new place to print to is a two-line function rather
 * than another hundred.
 *
 * What it understands is what a bootloader actually prints: %s %c %d %u %x
 * %X %%, each with an optional field width of up to eight digits, an optional
 * leading zero and an optional '-' to left-align.  There are no floats, no
 * lengths and no argument reordering, because nothing here needs them.
 */
#include "spl.h"
#include "format.h"

/* Emit `s` in a field `width` wide, on the left if asked and on the right if
 * not, which is all a field width has ever meant. */
static void put_field(spl_emit emit, void *arg, const char *s, int width,
                      int left)
{
    int n = 0;
    int i;

    while (s[n]) {
        n++;
    }
    if (!left) {
        for (i = n; i < width; i++) {
            emit(' ', arg);
        }
    }
    for (i = 0; i < n; i++) {
        emit(s[i], arg);
    }
    if (left) {
        for (i = n; i < width; i++) {
            emit(' ', arg);
        }
    }
}

static void put_dec(spl_emit emit, void *arg, s32 v, int width, int left)
{
    char buf[12];
    int n = 0;
    u32 u = (v < 0) ? (u32)-v : (u32)v;

    if (v < 0) {
        buf[n++] = '-';
    }
    do {
        buf[n++] = (char)('0' + u % 10);
        u /= 10;
    } while (u);

    while (!left && n < width) {
        emit(' ', arg);
        n++;
    }
    while (n) {
        emit(buf[--n], arg);
    }
}

static void put_hex(spl_emit emit, void *arg, u32 v, int digits, int upper,
                    int width, int left)
{
    static const char lo[] = "0123456789abcdef";
    static const char up[] = "0123456789ABCDEF";
    const char *hex = upper ? up : lo;

    while (!left && digits < width) {
        emit(' ', arg);
        width--;
    }
    for (int i = digits - 1; i >= 0; i--) {
        emit(hex[(v >> (i * 4)) & 0xf], arg);
    }
}

void spl_vformat(spl_emit emit, void *arg, const char *fmt,
                 __builtin_va_list ap)
{
    while (*fmt) {
        int width = 0;
        int left = 0;

        if (*fmt != '%') {
            emit(*fmt++, arg);
            continue;
        }
        fmt++;

        if (*fmt == '-') {
            left = 1;
            fmt++;
        }
        if (*fmt == '0') {
            fmt++;                     /* a leading zero is just zero      */
        }
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt++ - '0');
            if (width > 8) {
                width = 8;
            }
        }

        switch (*fmt) {
        case 's': {
            const char *s = __builtin_va_arg(ap, const char *);

            put_field(emit, arg, s ? s : "(null)", width, left);
            break;
        }
        case 'c':
            emit((char)__builtin_va_arg(ap, int), arg);
            break;
        case 'd':
            put_dec(emit, arg, __builtin_va_arg(ap, s32), width, left);
            break;
        case 'u':
            put_dec(emit, arg, (s32)__builtin_va_arg(ap, u32), width, left);
            break;
        case 'x':
            put_hex(emit, arg, __builtin_va_arg(ap, u32),
                    width ? width : 8, 0, width, left);
            break;
        case 'X':
            put_hex(emit, arg, __builtin_va_arg(ap, u32),
                    width ? width : 8, 1, width, left);
            break;
        case '%':
            emit('%', arg);
            break;
        case '\0':
            return;
        default:
            emit('%', arg);
            emit(*fmt, arg);
            break;
        }
        fmt++;
    }
}

/* ---- spl_format's sink: everything that fits, and a count of the rest -- */

struct bounded {
    char *buf;
    unsigned long size;
    unsigned long used;
};

static void bounded_emit(char c, void *arg)
{
    struct bounded *b = arg;

    if (b->used + 1 < b->size) {
        b->buf[b->used++] = c;
    } else {
        b->used++;                    /* count it even though it is lost */
    }
}

int spl_format(char *dst, unsigned long size, const char *fmt, ...)
{
    struct bounded b = { dst, size, 0 };
    __builtin_va_list ap;

    __builtin_va_start(ap, fmt);
    spl_vformat(bounded_emit, &b, fmt, ap);
    __builtin_va_end(ap);

    if (size) {
        dst[b.used < size ? b.used : size - 1] = '\0';
    }
    return (int)b.used;
}