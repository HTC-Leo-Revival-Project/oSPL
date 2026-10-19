/*
 * boardid.h - what this board is, and telling the OS.
 */
#ifndef BOARDID_H
#define BOARDID_H

#include "spl.h"

/*
 * Work out which board this is and leave it where the OS's drivers read it
 * from: the globals page at the bottom of SDRAM.
 *
 * The answers come off the power chip, which the factory programmed, and fall
 * back to what board.h says this board is when nothing is there.  The panel
 * is a constant for now - the real bootloader asks the panel over its own
 * link, and there is no such link here to ask on.
 */
void board_globals_fill(void);

#endif /* BOARDID_H */
