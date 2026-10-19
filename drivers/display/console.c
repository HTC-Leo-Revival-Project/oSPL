/*
 * console.c - text on the panel.
 *
 * The LCDC is scanning the DMA_P buffer a frame at a time, so drawing is
 * writing RGB565 pixels into that buffer and letting the next frame pick
 * them up.  There is no double buffering and no way to ask what is already
 * there, so every character is written whole: a glyph is ten rows of a byte
 * and each is turned into six pixels.
 *
 * Two things sit on top of that: text_at() puts a string at a pixel position,
 * which is what a screen laid out by hand wants, and the console below it
 * keeps a cursor and scrolls, which is what a prompt wants.
 */
#include "spl.h"
#include "board.h"
#include "font.h"
#include "console.h"
#include "format.h"

static u16 *fb(void)
{
    return (u16 *)(u32)FB_BASE;
}

void screen_fill(u16 colour)
{
    u16 *p = fb();

    for (u32 i = 0; i < PANEL_WIDTH * PANEL_HEIGHT; i++) {
        p[i] = colour;
    }
}

static void fill_row(int row, u16 colour)
{
    u16 *p = fb() + row * PANEL_WIDTH;

    for (int c = 0; c < (int)PANEL_WIDTH; c++) {
        p[c] = colour;
    }
}

/* The glyph for `c`, or NULL for anything the font does not have. */
static const u8 *glyph_for(int c)
{
    if (c < FONT_FIRST || c > FONT_FIRST + FONT_COUNT - 1) {
        return NULL;
    }
    return font_table[c - FONT_FIRST].rows;
}

static void put_pixel(int x, int y, u16 colour)
{
    if (x >= 0 && x < (int)PANEL_WIDTH && y >= 0 && y < (int)PANEL_HEIGHT) {
        fb()[y * PANEL_WIDTH + x] = colour;
    }
}

void text_at(int x, int y, u16 colour, const char *s)
{
    for (; *s; s++) {
        const u8 *rows = glyph_for(*s);
        int r, b;

        if (rows) {
            for (r = 0; r < FONT_H; r++) {
                for (b = 0; b < FONT_W; b++) {
                    if (rows[r] & (0x80 >> b)) {
                        put_pixel(x + b, y + r, colour);
                    }
                }
            }
        }
        x += FONT_W;
    }
}

int text_width(const char *s)
{
    int n = 0;

    while (s[n]) {
        n++;
    }
    return n * FONT_W;
}

/* ---- the scrolling console ------------------------------------------- */

static int con_col;
static int con_row;
static u16 con_fg = 0xffff;
static u16 con_bg = 0x0000;

void console_init(void)
{
    con_col = 0;
    con_row = 0;
}

void console_goto(int col, int row)
{
    con_col = col;
    con_row = row;
}

void console_where(int *col, int *row)
{
    if (col) {
        *col = con_col;
    }
    if (row) {
        *row = con_row;
    }
}

/*
 * Move every line up one by copying the ones below it down, which is the same
 * amount of work as redrawing the screen and does not need the console to
 * remember what it has already said.
 */
static void scroll_up(void)
{
    u16 *p = fb();
    int lines = font_rows() - 1;
    unsigned long bytes = (unsigned long)lines * PANEL_WIDTH * FONT_H *
                          sizeof(u16);

    if (lines > 0) {
        memmove(p, p + (unsigned long)PANEL_WIDTH * FONT_H, bytes);
    }
    fill_row(font_rows() - 1, con_bg);
}

static void newline(void)
{
    con_col = 0;
    if (++con_row >= font_rows()) {
        scroll_up();
        con_row = font_rows() - 1;
    }
}

void console_putc(char c)
{
    char one[2];

    switch (c) {
    case '\r':
        con_col = 0;
        return;
    case '\n':
        newline();
        return;
    case '\b':
        if (con_col) {
            con_col--;
        }
        return;
    default:
        break;
    }

    one[0] = c;
    one[1] = '\0';
    text_at(con_col * FONT_W, con_row * FONT_H, con_fg, one);

    if (++con_col >= font_columns()) {
        newline();
    }
}

void console_puts(const char *s)
{
    for (; *s; s++) {
        console_putc(*s);
    }
}

static void console_emit(char c, void *arg)
{
    (void)arg;
    console_putc(c);
}

void console_printf(const char *fmt, ...)
{
    __builtin_va_list ap;

    __builtin_va_start(ap, fmt);
    spl_vformat(console_emit, NULL, fmt, ap);
    __builtin_va_end(ap);
}
