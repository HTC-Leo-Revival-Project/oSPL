/*
 * start.c - where the boot ROM drops us.
 *
 * The boot ROM copies this image to physical 0 and jumps to it in SVC mode
 * with the MMU off, the caches as it left them, and no stack of its own worth
 * the name.  So: set a stack and come in through spl_entry().
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

/* The linker script puts the stack at the top of the window we use. */
extern u32 __stack_top;
extern u32 __bss_start;
extern u32 __bss_end;

void spl_entry(void);

__attribute__((section(".text.boot"), naked, used))
void _start(void)
{
    /*
     * Nothing but this: set up the stack and go.  A naked function whose
     * inline asm needs addresses is a literal pool in the middle of the
     * program, and the pool is not where anyone reading the entry point
     * expects it to be.  The stack is the one thing that has to happen
     * before there is anywhere to put anything else.
     */
    __asm__ volatile(
        "ldr sp, =__stack_top\n"
        "b spl_entry\n"
    );
}
