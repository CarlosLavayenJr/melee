/* pc_printf.c — a small printf family for host builds.
 *
 * src/MSL/printf.c compiles but does not link: it is written against MWCC's
 * varargs intrinsics (__builtin_va_info and friends), which are compiler
 * internals with no counterpart in GCC or Clang. So MSL's printf is excluded
 * from host builds and this replaces it.
 *
 * Deliberately small. This exists so OSReport can narrate boot; it is not a
 * conforming printf. Supported: %d %i %u %x %X %c %s %p %%, an optional width
 * with zero padding, and the l/ll length modifiers. %f prints a fixed six
 * decimals. Anything else is emitted verbatim so a bad format is visible
 * rather than silent.
 */
#include "pc_sys.h"

#include <stdarg.h>
#include <stddef.h>

typedef struct {
    char* buf;    /* NULL means write straight to the log */
    size_t cap;   /* space in buf, including the terminator */
    size_t len;   /* characters produced, terminator excluded */
} sink;

static void emit(sink* s, char c)
{
    if (s->buf != NULL) {
        if (s->len + 1 < s->cap) {
            s->buf[s->len] = c;
        }
    } else {
        char one[2];
        one[0] = c;
        one[1] = '\0';
        pc_sys_log(one);
    }
    s->len++;
}

static void emit_str(sink* s, const char* p)
{
    if (p == NULL) {
        p = "(null)";
    }
    while (*p != '\0') {
        emit(s, *p++);
    }
}

static void emit_num(sink* s, unsigned long long v, unsigned base, int upper,
                     int width, int zero_pad, int negative)
{
    char tmp[24];
    const char* digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    int n = 0;
    int pad;

    do {
        tmp[n++] = digits[v % base];
        v /= base;
    } while (v != 0 && n < (int) sizeof(tmp));

    pad = width - n - (negative ? 1 : 0);

    if (negative && zero_pad) {
        emit(s, '-');
    }
    while (pad-- > 0) {
        emit(s, zero_pad ? '0' : ' ');
    }
    if (negative && !zero_pad) {
        emit(s, '-');
    }
    while (n-- > 0) {
        emit(s, tmp[n]);
    }
}

static void emit_fixed(sink* s, double d)
{
    unsigned long long whole;
    unsigned long long frac;
    int i;

    if (d < 0) {
        emit(s, '-');
        d = -d;
    }
    whole = (unsigned long long) d;
    d -= (double) whole;
    for (i = 0; i < 6; i++) {
        d *= 10.0;
    }
    frac = (unsigned long long) (d + 0.5);

    emit_num(s, whole, 10, 0, 0, 0, 0);
    emit(s, '.');
    emit_num(s, frac, 10, 0, 6, 1, 0);
}

static void format(sink* s, const char* fmt, va_list ap)
{
    while (*fmt != '\0') {
        int width = 0, zero_pad = 0, longs = 0;

        if (*fmt != '%') {
            emit(s, *fmt++);
            continue;
        }
        fmt++;

        if (*fmt == '0') {
            zero_pad = 1;
            fmt++;
        }
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt++ - '0');
        }
        while (*fmt == 'l') {
            longs++;
            fmt++;
        }

        switch (*fmt) {
        case 'd':
        case 'i': {
            long long v = longs > 1 ? va_arg(ap, long long)
                                    : (long long) va_arg(ap, long);
            int neg = v < 0;
            emit_num(s, neg ? (unsigned long long) -v : (unsigned long long) v,
                     10, 0, width, zero_pad, neg);
            break;
        }
        case 'u':
            emit_num(s,
                     longs > 1 ? va_arg(ap, unsigned long long)
                               : (unsigned long long) va_arg(ap, unsigned long),
                     10, 0, width, zero_pad, 0);
            break;
        case 'x':
        case 'X':
            emit_num(s,
                     longs > 1 ? va_arg(ap, unsigned long long)
                               : (unsigned long long) va_arg(ap, unsigned long),
                     16, *fmt == 'X', width, zero_pad, 0);
            break;
        case 'p':
            emit_str(s, "0x");
            emit_num(s, (unsigned long long) (size_t) va_arg(ap, void*), 16, 0,
                     0, 0, 0);
            break;
        case 'c':
            emit(s, (char) va_arg(ap, int));
            break;
        case 's':
            emit_str(s, va_arg(ap, const char*));
            break;
        case 'f':
        case 'g':
        case 'e':
            emit_fixed(s, va_arg(ap, double));
            break;
        case '%':
            emit(s, '%');
            break;
        default:
            emit(s, '%');
            emit(s, *fmt);
            break;
        }
        if (*fmt != '\0') {
            fmt++;
        }
    }
}

int vsprintf(char* out, const char* fmt, va_list ap)
{
    sink s;
    s.buf = out;
    s.cap = (size_t) -1;
    s.len = 0;
    format(&s, fmt, ap);
    out[s.len] = '\0';
    return (int) s.len;
}

int vsnprintf(char* out, size_t n, const char* fmt, va_list ap)
{
    sink s;
    s.buf = out;
    s.cap = n;
    s.len = 0;
    format(&s, fmt, ap);
    if (n > 0) {
        out[s.len < n ? s.len : n - 1] = '\0';
    }
    return (int) s.len;
}

int vprintf(const char* fmt, va_list ap)
{
    sink s;
    s.buf = NULL;
    s.cap = 0;
    s.len = 0;
    format(&s, fmt, ap);
    return (int) s.len;
}

int sprintf(char* out, const char* fmt, ...)
{
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vsprintf(out, fmt, ap);
    va_end(ap);
    return n;
}

int snprintf(char* out, size_t cap, const char* fmt, ...)
{
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vsnprintf(out, cap, fmt, ap);
    va_end(ap);
    return n;
}

int printf(const char* fmt, ...)
{
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vprintf(fmt, ap);
    va_end(ap);
    return n;
}
