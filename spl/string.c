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
