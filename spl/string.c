/*
 * string.c - the three string routines a bootloader needs.
 */
#include "spl.h"

void *memcpy(void *dst, const void *src, unsigned long n)
{
    u8 *d = (u8 *)dst;
    const u8 *s = (const u8 *)src;

    while (n--) {
        *d++ = *s++;
    }
    return dst;
}

/*
 * Overlapping, and the console scrolls upwards - the destination is below the
 * source - so this copies forwards a byte at a time.  Doing it a word at a
 * time would walk over the tail of the region it is still going to read.
 */
void *memmove(void *dst, const void *src, unsigned long n)
{
    u8 *d = (u8 *)dst;
    const u8 *s = (const u8 *)src;

    if (d < s) {
        while (n--) {
            *d++ = *s++;
        }
    } else {
        d += n;
        s += n;
        while (n--) {
            *--d = *--s;
        }
    }
    return dst;
}

void *memset(void *dst, int c, unsigned long n)
{
    u8 *d = (u8 *)dst;

    while (n--) {
        *d++ = (u8)c;
    }
    return dst;
}

int memcmp(const void *a, const void *b, unsigned long n)
{
    const u8 *x = (const u8 *)a, *y = (const u8 *)b;

    while (n--) {
        if (*x != *y) {
            return (int)*x - (int)*y;
        }
        x++;
        y++;
    }
    return 0;
}
