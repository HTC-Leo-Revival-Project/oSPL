/*
 * div.c - integer division.
 *
 * There is no runtime underneath this program to call when an integer divide
 * happens, and the compiler does not have one either - it emits a call to the
 * EABI helper and expects something to be there.  The PLL frequencies need it
 * and anything else that works in MHz will too, so here it is.
 *
 * The EABI also says what to do about the cases that have no answer: dividing
 * by zero returns zero rather than trapping, because the alternative in a
 * bootloader is a reset in the middle of somebody's flash write.
 */
#include "spl.h"

/*
 * Divide `a` by `b`, returning the quotient and, if `rem` is not null, the
 * remainder.  Restoring shift and subtract: bring bits down from the top until
 * there is one to try subtracting, then keep one step ahead of the bit.
 */
static u32 udivmod(u32 a, u32 b, u32 *rem)
{
    u32 q = 0;
    u32 r = 0;
    int i;

    if (b == 0) {
        if (rem) {
            *rem = 0;
        }
        return 0;
    }
    for (i = 31; i >= 0; i--) {
        r = (r << 1) | ((a >> i) & 1u);
        if (r >= b) {
            r -= b;
            q |= 1u << i;
        }
    }
    if (rem) {
        *rem = r;
    }
    return q;
}

u32 __aeabi_uidiv(u32 a, u32 b)
{
    return udivmod(a, b, NULL);
}

u32 __aeabi_uidivmod(u32 a, u32 b, u32 *rem)
{
    return udivmod(a, b, rem);
}

s32 __aeabi_idiv(s32 a, s32 b)
{
    int neg = 0;
    u32 ua, ub;
    s32 q;

    if (a < 0) {
        ua = 0u - (u32)a;      /* negate without a wider type */
        neg = !neg;
    } else {
        ua = (u32)a;
    }
    if (b < 0) {
        ub = 0u - (u32)b;
        neg = !neg;
    } else {
        ub = (u32)b;
    }

    q = (s32)udivmod(ua, ub, NULL);
    return neg ? -q : q;
}

s32 __aeabi_idivmod(s32 a, s32 b, s32 *rem)
{
    int neg = 0;
    u32 ua, ub, r = 0;
    s32 q;

    if (a < 0) {
        ua = 0u - (u32)a;      /* negate without a wider type */
        neg = !neg;
    } else {
        ua = (u32)a;
    }
    if (b < 0) {
        ub = 0u - (u32)b;
        neg = !neg;
    } else {
        ub = (u32)b;
    }

    q = (s32)udivmod(ua, ub, &r);
    if (rem) {
        *rem = neg ? -(s32)r : (s32)r;
    }
    return neg ? -q : q;
}