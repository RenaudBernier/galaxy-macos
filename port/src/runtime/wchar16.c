// 16-bit wchar_t string functions for game code.
//
// The game is compiled with -fshort-wchar (wchar_t is 16 bits, as on the
// Wii); the host C library assumes 32-bit wchar_t. The prelude renames the
// game's calls to these Port*16 functions, so host code is unaffected.
// Compiled with -fshort-wchar (see port/CMakeLists.txt).

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

_Static_assert(sizeof(wchar_t) == 2, "wchar16.c must be built with -fshort-wchar");

size_t PortWcslen16(const wchar_t* s) {
    size_t n = 0;
    while (s[n] != 0) {
        n++;
    }
    return n;
}

wchar_t* PortWcsncpy16(wchar_t* dst, const wchar_t* src, size_t n) {
    size_t i = 0;
    for (; i < n && src[i] != 0; i++) {
        dst[i] = src[i];
    }
    for (; i < n; i++) {
        dst[i] = 0;
    }
    return dst;
}

wchar_t* PortWcscpy16(wchar_t* dst, const wchar_t* src) {
    size_t i = 0;
    do {
        dst[i] = src[i];
    } while (src[i++] != 0);
    return dst;
}

int PortWcscmp16(const wchar_t* a, const wchar_t* b) {
    while (*a != 0 && *a == *b) {
        a++;
        b++;
    }
    return (int)(unsigned short)*a - (int)(unsigned short)*b;
}

int PortWcsncmp16(const wchar_t* a, const wchar_t* b, size_t n) {
    for (; n > 0; n--, a++, b++) {
        if (*a != *b || *a == 0) {
            return (int)(unsigned short)*a - (int)(unsigned short)*b;
        }
    }
    return 0;
}

wchar_t* PortWcscat16(wchar_t* dst, const wchar_t* src) {
    PortWcscpy16(dst + PortWcslen16(dst), src);
    return dst;
}

wchar_t* PortWcschr16(const wchar_t* s, wchar_t c) {
    for (;; s++) {
        if (*s == c) {
            return (wchar_t*)s;
        }
        if (*s == 0) {
            return NULL;
        }
    }
}

// vswprintf: each conversion is formatted with the host's narrow printf and
// widened; literal text is copied as 16-bit units. %s takes a narrow string,
// %ls a 16-bit string, %c/%lc a character.
int PortVswprintf16(wchar_t* dst, size_t n, const wchar_t* fmt, va_list ap) {
    size_t out = 0;
#define PUT(ch)                                                                                                        \
    do {                                                                                                               \
        if (out + 1 < n) {                                                                                             \
            dst[out] = (wchar_t)(ch);                                                                                  \
        }                                                                                                              \
        out++;                                                                                                         \
    } while (0)

    while (*fmt != 0) {
        if (*fmt != L'%') {
            PUT(*fmt++);
            continue;
        }
        // Collect the conversion spec as narrow chars.
        char spec[32];
        size_t sl = 0;
        spec[sl++] = '%';
        fmt++;
        while (*fmt != 0 && strchr("-+ #0123456789.*", (char)*fmt) != NULL && sl < sizeof(spec) - 4) {
            if (*fmt == L'*') {
                sl += (size_t)snprintf(spec + sl, sizeof(spec) - sl, "%d", va_arg(ap, int));
                fmt++;
            } else {
                spec[sl++] = (char)*fmt++;
            }
        }
        int longMod = 0;
        int hMod = 0;
        while (*fmt == L'l' || *fmt == L'h') {
            if (*fmt == L'l') {
                longMod++;
            } else {
                hMod++;
            }
            fmt++;
        }
        const wchar_t conv = *fmt != 0 ? *fmt++ : 0;
        char buf[512];
        buf[0] = 0;
        switch (conv) {
        case L'%':
            PUT(L'%');
            continue;
        case L'd':
        case L'i':
        case L'u':
        case L'x':
        case L'X':
        case L'o': {
            if (longMod >= 2) {
                spec[sl++] = 'l';
                spec[sl++] = 'l';
            }
            spec[sl++] = (char)conv;
            spec[sl] = 0;
            if (longMod >= 2) {
                snprintf(buf, sizeof(buf), spec, va_arg(ap, long long));
            } else if (hMod == 1 && (conv == L'd' || conv == L'i')) {
                snprintf(buf, sizeof(buf), spec, (short)va_arg(ap, int));
            } else {
                snprintf(buf, sizeof(buf), spec, va_arg(ap, int));
            }
            break;
        }
        case L'f':
        case L'F':
        case L'g':
        case L'G':
        case L'e':
        case L'E':
            spec[sl++] = (char)conv;
            spec[sl] = 0;
            snprintf(buf, sizeof(buf), spec, va_arg(ap, double));
            break;
        case L'c':
            PUT(va_arg(ap, int));
            continue;
        case L's':
            if (longMod) {
                const wchar_t* s = va_arg(ap, const wchar_t*);
                if (s == NULL) {
                    s = L"(null)";
                }
                // Apply precision (max chars) if given.
                long maxc = -1;
                const char* dot = strchr(spec, '.');
                if (dot != NULL) {
                    maxc = strtol(dot + 1, NULL, 10);
                }
                for (long i = 0; s[i] != 0 && (maxc < 0 || i < maxc); i++) {
                    PUT(s[i]);
                }
                continue;
            } else {
                spec[sl++] = 's';
                spec[sl] = 0;
                const char* s = va_arg(ap, const char*);
                snprintf(buf, sizeof(buf), spec, s != NULL ? s : "(null)");
            }
            break;
        case L'p':
            snprintf(buf, sizeof(buf), "%p", va_arg(ap, void*));
            break;
        default:
            continue;
        }
        for (const char* p = buf; *p != 0; p++) {
            PUT((unsigned char)*p);
        }
    }
    if (n > 0) {
        dst[out < n ? out : n - 1] = 0;
    }
#undef PUT
    return out < n ? (int)out : -1;
}

int PortSwprintf16(wchar_t* dst, size_t n, const wchar_t* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = PortVswprintf16(dst, n, fmt, ap);
    va_end(ap);
    return r;
}
