/*
 * main.c - the bootloader.
 *
 * What a phone does between the core coming out of reset and the OS starting:
 * say what we are, and if there is nothing more to do, say that too.
 *
 * The real HSPL does much more - it brings the clocks and the PMIC up, reads
 * the panel and the EEPROM over I2C, builds a bad block table, DMOVs the
 * image with the data mover and paints a splash screen - and all of that
 * belongs in this tree too.  What is here now is the front of it: somewhere
 * to say things.
 */
#include "spl.h"
#include "board.h"

#include "drivers/uart/uart.h"

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
    uart_init();
    uart_puts("\r\nSPL: starting\r\n");

    uart_puts("SPL: nothing to do yet\r\n");
    stop();
}