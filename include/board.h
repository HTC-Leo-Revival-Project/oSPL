/*
 * board.h - what the QSD8250 (Leo) looks like from an SPL.
 *
 * Only the windows this bootloader actually touches.  The full map is in
 * unicorn/leo_memmap.html; the names here are the ones that map gives them.
 * Nothing here knows or cares what the OS image turns out to be.
 */
#ifndef BOARD_H
#define BOARD_H

/* ---- SDRAM ---------------------------------------------------------- */
/*
 * The SPL runs with the MMU off, so every address below is a physical one.
 * The boot ROM leaves the core here, at physical 0 with translation off and
 * the caches as it found them.
 */
#define RAM_BASE            0x00000000u
#define RAM_END             0x28000000u  /* end of SDRAM bank1_3 */
#define SPL_STACK_TOP       0x08000000u  /* our stack, clear of everything */

/* Where the OS image is put, and where it is entered. */
#define OS_IMAGE_BASE       0x11800000u
#define OS_IMAGE_MAX        0x0A000000u  /* "clear 10MB ext RAM" */

/* ---- the OS area in NAND -------------------------------------------- */
/*
 * The bootloader itself is flashed from block 0x20a, one 2048-byte sector
 * per page, and is read by the boot ROM.  This bootloader takes the OS
 * image out of the "OS" area, which starts at block 0x212 in the same
 * layout: a WinCE MBR, an MSFLSH50 tag, then the image.
 */
#define NAND_SPL_BLOCK      0x20au
#define NAND_OS_BLOCK       0x212u

/* ---- peripherals ---------------------------------------------------- */
#define UART1_BASE          0xA9A00000u  /* UART1DM, the console          */
#define NAND_BASE           0xA0A00000u  /* NAND + MPU                    */
#define MDP_BASE            0xAA200000u  /* display processor + LCDC      */
#define GPIO1_BASE          0xA9000000u
#define KITTEN_BASE         0xA8E00000u  /* VIC                           */
#define CSR_BASE            0xAC100000u  /* timers, A2M doorbells         */
#define ADM_BASE            0xA9700000u  /* security domain 3 (DMOV)      */
#define SIRC_BASE           0xAC200000u

/* GPIO1 pins the NAND's chip select and the NAND IRQ go to */
#define GPIO_NAND_CS        15
#define GPIO_NAND_IRQ       14

/* ---- the panel ------------------------------------------------------ */
/*
 * The HD2's LCD is 480x800.  The SPL sets the scanout to an RGB565 buffer
 * and hands the display to the OS, which paints from there.
 */
#define PANEL_WIDTH         480u
#define PANEL_HEIGHT        800u
#define FB_BASE             0x02A00000u
#define FB_STRIDE           (PANEL_WIDTH * 2u)      /* RGB565, bytes a line */

/* ---- what the SPL leaves in the board globals ------------------------ */
/*
 * A page at the bottom of SDRAM the board's drivers read their identity out
 * of: the machine it is, the panel it is driving, and why it powered on.  A
 * bootloader is expected to fill these in before handing over; the OS reads
 * them rather than guessing.
 */
#define GLOBALS_BASE        0x00080000u
#define GLOBALS_BOARD_ID    (GLOBALS_BASE + 0x1030u)
#define GLOBALS_PANEL_TYPE  (GLOBALS_BASE + 0x1034u)
#define GLOBALS_POWER_ON    (GLOBALS_BASE + 0x105cu)

/* This phone: board 4, the Sharp panel.  The real bootloader asks the panel
 * over MDDI which one it has; there is nothing to ask yet. */
#define BOARD_ID            4u
#define PANEL_TYPE_SHARP    10u
#define POWER_ON_BUTTON     0x42555454u     /* 'TTUB', spelled backwards */

#endif /* BOARD_H */
