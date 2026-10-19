/*
 * main.c - the bootloader.
 *
 * What a phone does between the core coming out of reset and the OS starting:
 * say what we are, identify the flash, and find the OS image in it.
 *
 * The real HSPL does much more - it brings the clocks and the PMIC up, reads
 * the panel and the EEPROM over I2C, builds a bad block table, DMOVs the
 * image with the data mover and paints a splash screen - and all of that
 * belongs in this tree too.  What is here now is the front of it.
 */
#include "spl.h"
#include "board.h"

#include "drivers/uart/uart.h"
#include "drivers/nand/nand.h"

#include "osimage.h"

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

void spl_main(void)
{
    struct nand_info nand;
    struct os_image img;

    uart_init();
    uart_puts("\r\nSPL: starting\r\n");

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
     * and how long it is.
     */
    if (osimage_locate(&img)) {
        uart_puts("SPL: no OS image to boot\r\n");
        stop();
    }
    uart_printf("SPL: OS image, %u sectors of %u, at 0x%08x\r\n",
                img.sectors, NAND_PAGE_SIZE, img.base);

    uart_puts("SPL: nothing to do with it yet\r\n");
    stop();
}