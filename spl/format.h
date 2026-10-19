/*
 * format.h - the one printf.
 */
#ifndef SPL_FORMAT_H
#define SPL_FORMAT_H

#include "spl.h"

/*
 * A place to print to: a function that takes a character and whatever it
 * needs to know about where it is going.  This is what lets one conversion
 * loop serve the console, the UART and the bootloader talking to both.
 */
typedef void (*spl_emit)(char c, void *arg);

/* Format `fmt` into `ap`, one character at a time, into `emit`. */
void spl_vformat(spl_emit emit, void *arg, const char *fmt,
                 __builtin_va_list ap);

/*
 * Format into a buffer, always NUL terminating.  Returns how many characters
 * would have been written, so a truncated result can be spotted.
 */
int spl_format(char *dst, unsigned long size, const char *fmt, ...);

#endif /* SPL_FORMAT_H */