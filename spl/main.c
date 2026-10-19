/*
 * main.c - the bootloader.
 *
 * What a phone does between the core coming out of reset and the OS starting:
 * say what we are, bring the panel up, identify the flash, find the OS image
 * in it, put the image where it belongs, and jump to it.
 *
 * The real HSPL does more - it brings the clocks and the PMIC up, reads the
 * panel and the EEPROM over I2C, builds a bad block table, DMOVs the image
 * with the data mover and paints a splash screen - and all of that belongs in
 * this tree too.  What is here now is the part that actually gets an image
 * running and a panel lit, in the same order the real one does it.
 */
#include "spl.h"
#include "board.h"

#include "drivers/uart/uart.h"
#include "drivers/nand/nand.h"
#include "drivers/display/display.h"
#include "drivers/timer/timer.h"

#include "osimage.h"
#include "boot.h"

/*
 * Nothing below can usefully carry on without what it is about to do, so
 * every failure ends the same way: say what went wrong, then stop.  The core
 * stays where it is, which is as close to "off" as a bootloader gets without
 * a way to turn the rails off.
 */
static void stop(void)
{
    for (;;) {
    }
}

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
    struct nand_info nand;
    struct os_image img;
    u32 clear;

    uart_init();
    uart_puts("\r\nSPL: starting\r\n");

    /*
     * The panel goes up first.  If there is going to be anything on the
     * glass, it is worth the display already scanning out before spending
     * however long the load takes.
     */
    display_init();
    board_identify();

    /*
     * Identify the flash.  Everything after this depends on it, so stop here
     * rather than read a part we cannot drive.
     */
    if (nand_init(&nand)) {
        uart_puts("SPL: no usable NAND flash\r\n");
        stop();
    }
    uart_printf("SPL: flash maker=0x%02x device=0x%02x, %u blocks of %u "
                "pages\r\n", nand.id.maker, nand.id.device,
                nand.num_blocks, nand.pages_per_block);

    /*
     * Find the OS image: the area's own MBR says which sector it starts at
     * and how long it is.  That is the last thing that can go wrong quietly.
     */
    if (osimage_locate(&img)) {
        uart_puts("SPL: no OS image to boot\r\n");
        stop();
    }
    uart_printf("SPL: OS image, %u sectors of %u, at 0x%08x\r\n",
                img.sectors, NAND_PAGE_SIZE, img.base);

    /*
     * Clear where it is going to land first.  The image can be up to ten
     * megabytes, and whatever was in RAM before is not something to hand on.
     */
    clear = img.size > OS_IMAGE_MAX ? OS_IMAGE_MAX : img.size;
    uart_printf("SPL: clearing %u bytes at 0x%08x\r\n", clear, img.base);
    memset((void *)(u32)img.base, 0xff, clear);

    /* And put the image there. */
    uart_puts("SPL: loading\r\n");
    if (osimage_load(&img)) {
        uart_puts("SPL: read failed\r\n");
        stop();
    }
    uart_puts("SPL: loaded\r\n");

    /* Give the console a moment to drain, then hand over. */
    mdelay(50);
    uart_printf("SPL: jumping to 0x%08x\r\n", img.base);
    boot_jump(img.base);
}