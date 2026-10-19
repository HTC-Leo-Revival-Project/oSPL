/*
 * main.c - the bootloader.
 *
 * What a phone does between the core coming out of reset and the OS starting:
 * say what we are, bring the panel up, tell the board what it is, and then
 * either become the bootloader or load the OS and get out of the way.
 *
 * The real HSPL does much more - it brings the clocks and the PMIC up, reads
 * the panel and the EEPROM over I2C, builds a bad block table, DMOVs the
 * image with the data mover and paints a splash screen - and all of that
 * belongs in this tree too.  What is here now is the front of it: the boot
 * decision, the menu behind it, and the load.
 */
#include "spl.h"
#include "board.h"

#include "drivers/uart/uart.h"
#include "drivers/keypad/keypad.h"
#include "drivers/display/display.h"

#include "bl.h"
#include "autoboot.h"

/*
 * Tell the OS what board it is on.  The real bootloader works most of this
 * out from the hardware - the board id from an EEPROM over I2C, the panel
 * from the MDDI link - and leaves the answers where the board's drivers
 * read them from.  Those answers are constants for now.
 */
static void board_identify(void)
{
    mmio_write32(GLOBALS_BOARD_ID, 0, BOARD_ID);
    mmio_write32(GLOBALS_PANEL_TYPE, 0, PANEL_TYPE_SHARP);
    mmio_write32(GLOBALS_POWER_ON, 0, POWER_ON_BUTTON);
}

void spl_main(void)
{
    uart_init();
    uart_puts("\r\nSPL: starting\r\n");

    /*
     * The panel goes up first.  If there is going to be anything on the
     * glass, it is worth the display already scanning out before spending
     * however long the rest of this takes.
     */
    display_init();
    board_identify();

    /*
     * Volume-down held down at reset is how the phone is asked to stop being
     * a way of reaching the OS and become its own bootloader.  It is one of
     * three ways in - the others are a tag the flashing tool leaves behind and
     * a tag in a flashed image that says there is something to go to - and it
     * is the only one of the three that a person holding the phone can do.
     */
    keypad_init();
    if (keypad_held(KEY_VOLDOWN, BL_HOLD_MS)) {
        uart_puts("SPL: volume-down held, staying in the bootloader\r\n");
        bl_enter();
    }

    autoboot();
}