/*
 * font.h - the bootloader's glyphs.
 */
#ifndef DISPLAY_FONT_H
#define DISPLAY_FONT_H

#include "spl.h"

/* One glyph: its character, and a byte per row, leftmost pixel in bit 7. */
struct font_glyph {
    int code;
    u8 rows[10];
};

extern const struct font_glyph font_table[];

#define FONT_W      6       /* pixels across a character cell */
#define FONT_H      10      /* ... and down                           */
#define FONT_FIRST  0x20    /* the first character with a glyph        */
#define FONT_COUNT  95      /* space through tilde                     */

int font_columns(void);     /* characters that fit across the panel */
int font_rows(void);        /* ... and down it                       */

#endif /* DISPLAY_FONT_H */