// MSL-compatible printf family for game code.
//
// The Wii's C library (MSL) differs from the host's in ways the game relies
// on; most importantly a NULL `%s` argument prints as "" (the host prints
// "(null)"), which the game uses when building paths. The prelude renames the
// game's sprintf/snprintf/vsprintf/vsnprintf to these functions. Each
// conversion is formatted with the host snprintf; only MSL's special cases are
// handled here.

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

int PortVsnprintfMsl(char* dst, size_t n, const char* fmt, va_list ap) {
    size_t out = 0;
#define PUTC(ch)                                                                                                       \
    do {                                                                                                               \
        if (out + 1 < n) {                                                                                             \
            dst[out] = (ch);                                                                                           \
        }                                                                                                              \
        out++;                                                                                                         \
    } while (0)

    while (*fmt != '\0') {
        if (*fmt != '%') {
            PUTC(*fmt++);
            continue;
        }
        char spec[48];
        size_t sl = 0;
        spec[sl++] = *fmt++;
        // flags, width, precision
        while (*fmt != '\0' && strchr("-+ #0", *fmt) != NULL && sl < sizeof(spec) - 16) {
            spec[sl++] = *fmt++;
        }
        if (*fmt == '*') {
            sl += (size_t)snprintf(spec + sl, sizeof(spec) - sl, "%d", va_arg(ap, int));
            fmt++;
        } else {
            while (*fmt >= '0' && *fmt <= '9' && sl < sizeof(spec) - 16) {
                spec[sl++] = *fmt++;
            }
        }
        if (*fmt == '.') {
            spec[sl++] = *fmt++;
            if (*fmt == '*') {
                sl += (size_t)snprintf(spec + sl, sizeof(spec) - sl, "%d", va_arg(ap, int));
                fmt++;
            } else {
                while (*fmt >= '0' && *fmt <= '9' && sl < sizeof(spec) - 16) {
                    spec[sl++] = *fmt++;
                }
            }
        }
        // length modifiers
        int lengthL = 0, lengthH = 0, lengthLL = 0, lengthBigL = 0;
        for (;;) {
            if (*fmt == 'l') {
                if (fmt[1] == 'l') {
                    lengthLL = 1;
                    fmt += 2;
                } else {
                    lengthL = 1;
                    fmt++;
                }
            } else if (*fmt == 'h') {
                lengthH++;
                fmt++;
            } else if (*fmt == 'L') {
                lengthBigL = 1;
                fmt++;
            } else {
                break;
            }
        }
        const char conv = *fmt != '\0' ? *fmt++ : '\0';
        char buf[512];
        buf[0] = '\0';
        switch (conv) {
        case '%':
            PUTC('%');
            continue;
        case 'd':
        case 'i':
        case 'u':
        case 'x':
        case 'X':
        case 'o':
        case 'c': {
            // On the Wii, long is 32 bits: %ld takes a 32-bit argument.
            if (lengthLL) {
                spec[sl++] = 'l';
                spec[sl++] = 'l';
                spec[sl++] = conv;
                spec[sl] = '\0';
                snprintf(buf, sizeof(buf), spec, va_arg(ap, long long));
            } else {
                if (lengthH == 1) {
                    spec[sl++] = 'h';
                } else if (lengthH >= 2) {
                    spec[sl++] = 'h';
                    spec[sl++] = 'h';
                }
                spec[sl++] = conv;
                spec[sl] = '\0';
                snprintf(buf, sizeof(buf), spec, va_arg(ap, int));
            }
            (void)lengthL;
            break;
        }
        case 'f':
        case 'F':
        case 'e':
        case 'E':
        case 'g':
        case 'G':
        case 'a':
        case 'A':
            spec[sl++] = conv;
            spec[sl] = '\0';
            if (lengthBigL) {
                snprintf(buf, sizeof(buf), spec, (double)va_arg(ap, long double));
            } else {
                snprintf(buf, sizeof(buf), spec, va_arg(ap, double));
            }
            break;
        case 's': {
            const char* s = va_arg(ap, const char*);
            if (s == NULL) {
                s = "";  // MSL
            }
            spec[sl++] = 's';
            spec[sl] = '\0';
            snprintf(buf, sizeof(buf), spec, s);
            break;
        }
        case 'p':
            snprintf(buf, sizeof(buf), "%p", va_arg(ap, void*));
            break;
        case 'n':
            *va_arg(ap, int*) = (int)out;
            continue;
        default:
            continue;
        }
        for (const char* p = buf; *p != '\0'; p++) {
            PUTC(*p);
        }
    }
    if (n > 0) {
        dst[out < n ? out : n - 1] = '\0';
    }
#undef PUTC
    return (int)out;
}

int PortSnprintfMsl(char* dst, size_t n, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = PortVsnprintfMsl(dst, n, fmt, ap);
    va_end(ap);
    return r;
}

int PortVsprintfMsl(char* dst, const char* fmt, va_list ap) { return PortVsnprintfMsl(dst, SIZE_MAX, fmt, ap); }

int PortSprintfMsl(char* dst, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = PortVsnprintfMsl(dst, SIZE_MAX, fmt, ap);
    va_end(ap);
    return r;
}
