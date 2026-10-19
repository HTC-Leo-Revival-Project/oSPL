/*
 * console.h - text on the panel.
 */
#ifndef CONSOLE_H
#define CONSOLE_H

#include "spl.h"

/* Fill the whole panel with one colour. */
void screen_fill(u16 colour);

/*
 * Draw one string at a pixel position, with the top left corner at (x, y).
 * Nothing is clipped except off the edge of the panel, so a caller drawing a
 * whole screen does not have to know how wide it is.
 */
void text_at(int x, int y, u16 colour, const char *s);

/* How wide a string will be, in pixels. */
int text_width(const char *s);

/*
 * A scrolling text console over the whole panel, which is what the
 * bootloader's prompt and everything it prints go through.  It starts at the
 * top left and scrolls a line when it runs off the bottom.
 */
void console_init(void);
void console_putc(char c);
void console_puts(const char *s);
void console_printf(const char *fmt, ...);

/* Move the console somewhere other than the top left, in characters. */
void console_goto(int col, int row);

/* Where the next character will go, in characters. */
void console_where(int *col, int *row);

#endif /* CONSOLE_H */