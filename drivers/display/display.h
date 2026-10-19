/*
 * display.h - the panel.
 */
#ifndef DISPLAY_H
#define DISPLAY_H

/*
 * Point the LCDC at an RGB565 buffer and start it scanning out.  After this
 * the panel shows whatever is written to that buffer, which is how the OS
 * gets a display to paint on.
 */
void display_init(void);

/* Fill the whole panel with one RGB565 colour. */
void display_clear(u16 colour);

#endif /* DISPLAY_H */