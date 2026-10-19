/*
 * bl.h - the bootloader menu.
 */
#ifndef BL_H
#define BL_H

#include "spl.h"

#define BL_LINE_MAX   128     /* a command line, in characters */
#define BL_MAX_ARGS   8       /* and how many words one can hold */

/* Say something on the panel and on the console at once. */
void bl_say(const char *s);
void bl_printf(const char *fmt, ...);

/* Parse a leading 0x-prefixed or decimal number, stopping at anything else. */
u32 bl_number(const char *s);

/* True if the two strings are the same. */
int bl_streq(const char *a, const char *b);

/*
 * Become the bootloader: draw the screen, print a prompt, and read and run
 * commands until something boots.  Does not return.
 */
void bl_enter(void);

#endif /* BL_H */