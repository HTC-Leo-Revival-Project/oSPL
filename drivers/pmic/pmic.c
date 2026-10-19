/*
 * pmic.c - the TPS65023, at I2C address 0x48.
 *
 * A power management chip with a register block the factory also stores the
 * board's configuration in.  It is reached the way the HSPL reaches it: a
 * write of the register number, then the value.
 *
 * Registers worth knowing about:
 *
 *   0x00  VERSION      what the chip says it is
 *   0x03  REG_CTRL     one bit per rail, and it remembers
 *   0x06  DEFCORE      the core voltage it comes up at
 *   0x10  ON, 0x11 OFF which rails are wanted
 *
 * and past the power registers, the board's own configuration - which slot
 * the board is, which panel is on it, and why it last powered on.  Those are
 * written once at the factory, which is why they are here and not in the
 * bootloader.
 */
#include "spl.h"
#include "board.h"
#include "i2c.h"
#include "pmic.h"

/* The HSPL writes the address with the direction bit still attached. */
#define PMIC_ADDR        0x48

#define REG_VERSION      0x00
#define REG_CTRL         0x03
#define REG_DEFCORE      0x06
#define REG_ON           0x10
#define REG_OFF          0x11

/* The board's own configuration, past the power registers. */
#define REG_BOARD_ID     0x20
#define REG_PANEL_TYPE   0x21
#define REG_POWER_ON     0x22

void pmic_identify(struct board_info *info)
{
    u8 v = 0;

    /*
     * Start from what this board is known to be.  Everything below can only
     * improve on that by answering, never by leaving it blank.
     */
    info->id = BOARD_ID;
    info->panel = PANEL_TYPE_SHARP;
    info->power_on = POWER_ON_BUTTON;
    info->present = 0;
    info->version = 0;

    if (i2c_read(PMIC_ADDR, REG_VERSION, &v, 1)) {
        return;                         /* nothing there: leave the defaults */
    }
    info->present = 1;
    info->version = v;

    /*
     * Everything else on this chip is optional: a board that has been
     * factory-programmed answers, and one that has not keeps the defaults.
     * Each read is its own transfer, so a register that is not there cannot
     * move the pointer another one was going to read from.
     */
    if (!i2c_read(PMIC_ADDR, REG_BOARD_ID, &v, 1) && v) {
        info->id = v;
    }
    v = 0;
    if (!i2c_read(PMIC_ADDR, REG_PANEL_TYPE, &v, 1) && v) {
        info->panel = v;
    }
    v = 0;
    if (!i2c_read(PMIC_ADDR, REG_POWER_ON, &v, 1) && v) {
        info->power_on = v;
    }
}

int pmic_rail(int level, int on)
{
    u8 ctrl;

    if (i2c_read(PMIC_ADDR, REG_CTRL, &ctrl, 1)) {
        return -1;
    }
    if (on) {
        ctrl |= 1u << level;
    } else {
        ctrl &= ~(1u << level);
    }
    return i2c_write(PMIC_ADDR, REG_CTRL, &ctrl, 1);
}