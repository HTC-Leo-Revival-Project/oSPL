/*
 * start.c - where the boot ROM drops us.
 *
 * The boot ROM copies this image to physical 0 and jumps to it in SVC mode
 * with the MMU off, the caches as it left them, and no stack of its own worth
 * the name.  So: set a stack, clear the bits of memory that are ours to
 * clear, call main, and if it ever comes back, stop.
 *
 * Keeping the MMU off all the way through is deliberate.  Everything this
 * bootloader touches - SDRAM, the UART, the NAND controller, the display -
 * is reachable at its own physical address, and the image it hands over to
 * expects to be entered at a physical address too.  There is nothing here
 * that needs a page table, and not building one is one less thing to get
 * wrong.
 */
#include "spl.h"
#include "board.h"

void spl_main(void);

/* The linker script puts the stack at the top of the window we use. */
extern u32 __stack_top;
extern u32 __bss_start;
extern u32 __bss_end;

__attribute__((section(".text.boot"), naked, used))
void _start(void)
{
    __asm__ volatile(
        "ldr sp, =__stack_top\n"
        "mov r11, #0\n"                     /* clear the frame pointer */

        /*
         * Zero .bss.  The ROM does not promise us RAM that reads as zero,
         * and everything in here assumes it: the flash scratch page the
         * driver reads a bad-block marker out of, above all.
         */
        "ldr r0, =__bss_start\n"
        "ldr r1, =__bss_end\n"
        "mov r2, #0\n"
        "bss_loop:\n"
        "cmp r0, r1\n"
        "bhs bss_done\n"
        "str r2, [r0], #4\n"                /* r0 and r2 must differ */
        "b bss_loop\n"
        "bss_done:\n"

        "bl  spl_main\n"
        "1:  b 1b\n"                        /* spl_main does not return */
    );
}