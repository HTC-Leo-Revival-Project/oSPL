/*
 * boardid.c - which board this is, and telling the OS about it.
 */
#include "spl.h"
#include "board.h"

#include "drivers/pmic/pmic.h"

#include "boardid.h"

/*
 * The globals page the OS reads the board's identity out of: which board it
 * is, which panel is on it, and why it powered on.  A bootloader is expected
 * to leave these before the hand-over, not after it, because the OS reads
 * them rather than working them out for itself.
 */
void board_globals_fill(void)
{
    struct board_info info;

    pmic_identify(&info);

    mmio_write32(GLOBALS_BOARD_ID, 0, info.id);
    mmio_write32(GLOBALS_PANEL_TYPE, 0, info.panel);
    mmio_write32(GLOBALS_POWER_ON, 0, info.power_on);
}
