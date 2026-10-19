/*
 * main.c - the bootloader.
 *
 * The boot ROM copies this image to physical 0 and jumps to it in SVC mode
 * with the MMU off and the caches as it left them.  start.c gets us here.
 *
 * Nothing happens yet.  This is the state where the program builds, links
 * and lands in the reset stub; the console comes next.
 */
#include "spl.h"
#include "board.h"

void spl_main(void)
{
    for (;;) {
    }
}