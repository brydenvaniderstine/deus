#include <stdarg.h>
#include <stdint.h>
#include "printf.h"
#include "../arch/x86/serial.h"

static void put_uint(uint64_t v, unsigned base, int width, char pad) {
    char buf[32];
    int i = 0;
    const char *digits = "0123456789abcdef";
    if (v == 0) buf[i++] = '0';
    while (v) {
        buf[i++] = digits[v % base];
        v /= base;
    }
    while (i < width) buf[i++] = pad;
    while (i--) serial_putc(buf[i]);
}

static void put_int(int64_t v, int width, char pad) {
    if (v < 0) {
        serial_putc('-');
        put_uint((uint64_t)(-v), 10, width, pad);
    } else {
        put_uint((uint64_t)v, 10, width, pad);
    }
}

void kprintf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    for (const char *p = fmt; *p; p++) {
        if (*p != '%') {
            serial_putc(*p);
            continue;
        }
        p++;
        int width = 0;
        char pad = ' ';
        if (*p == '0') { pad = '0'; p++; }
        while (*p >= '0' && *p <= '9') { width = width * 10 + (*p - '0'); p++; }
        /* length modifier: only 'l'/'ll' recognized, both treated as 64-bit */
        int longmod = 0;
        while (*p == 'l') { longmod++; p++; }

        switch (*p) {
            case 's': {
                const char *s = va_arg(ap, const char *);
                if (!s) s = "(null)";
                while (*s) serial_putc(*s++);
                break;
            }
            case 'c':
                serial_putc((char)va_arg(ap, int));
                break;
            case 'd': {
                int64_t v = longmod ? va_arg(ap, int64_t) : va_arg(ap, int32_t);
                put_int(v, width, pad);
                break;
            }
            case 'u': {
                uint64_t v = longmod ? va_arg(ap, uint64_t) : va_arg(ap, uint32_t);
                put_uint(v, 10, width, pad);
                break;
            }
            case 'x': {
                uint64_t v = longmod ? va_arg(ap, uint64_t) : va_arg(ap, uint32_t);
                put_uint(v, 16, width, pad);
                break;
            }
            case 'p': {
                uint64_t v = (uint64_t)va_arg(ap, void *);
                serial_puts("0x");
                put_uint(v, 16, 16, '0');
                break;
            }
            case '%':
                serial_putc('%');
                break;
            default:
                serial_putc('%');
                serial_putc(*p);
                break;
        }
    }
    va_end(ap);
}
