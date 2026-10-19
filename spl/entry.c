/*
 * entry.c - the first thing written in C.
 *
 * Two jobs: give ourselves somewhere to put the things that are not zeroed,
 * and put them at zero.  The boot ROM does not promise us RAM that reads as
 * zero, and everything in here assumes it - the flash scratch page the driver
 * reads a bad-block marker out of, above all.
 *
 * It is C rather than a loop in the reset stub because the reset stub's whole
 * job should be setting a stack and getting out of the way.  Done by hand it
 * needs the bounds as inline literals, and a naked function carrying a literal
 * pool does not lay out the way anything reading the entry point expects: the
 * first version of this did it in asm and the image came up in the middle of
 * its own printf.
 */
#include "spl.h"

extern u32 __bss_start;
extern u32 __bss_end;

void spl_main(void);

void spl_entry(void)
{
    u32 *p;

    for (p = &__bss_start; p < &__bss_end; p++) {
        *p = 0;
    }

    spl_main();
}
