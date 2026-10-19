/*
 * pmic.h - the power management chip.
 */
#ifndef PMIC_H
#define PMIC_H

#include "spl.h"

/*
 * Bring the TPS65023 up and read the board identity out of it.
 *
 * It is a power chip with a register block that the factory also stores the
 * board's configuration in, which is why the board id, the panel and the
 * reason the phone powered on come off this rather than out of the air: the
 * real bootloader reads them here and leaves them in the globals page for the
 * OS.
 *
 * `present` says whether anything answered.  With nothing on the bus the
 * answers fall back to what this board is known to be, which is a constant in
 * board.h rather than a guess - the machine knows which phone it is.
 */
struct board_info {
    u32 id;               /* which board this is                    */
    u32 panel;            /* which panel is on it                    */
    u32 power_on;         /* why it powered on                       */
    int present;          /* did the chip answer                     */
    u32 version;          /* its own version register, when it did   */
};

void pmic_identify(struct board_info *info);

/* Switch a rail on or off, by its bit in REG_CTRL. */
int pmic_rail(int level, int on);

#endif /* PMIC_H */