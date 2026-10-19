/*
 * spl.ld - the image the boot ROM copies to physical 0.
 *
 * The ROM jumps to offset 0, so .text.boot has to be the first thing in the
 * image.  Everything runs from SDRAM with the MMU off; there is no room
 * reserved for the stack because start.c picks one in board.h.
 *
 * The gap between the reset stub and everything else is not a gap.  The boot
 * ROM leaves a globals page at physical 0x80000 and, at 0x400, the coarse
 * table for the window at VA 0xb5080000 that those globals are read through.
 * The ROM fills both in before handing the core over.  In the phone's own
 * HSPL that space happens to be zero padding inside a half-megabyte image;
 * in ours it is not, and writing a page table over our own code makes the
 * image jump into the middle of a function.
 */

ENTRY(_start)

MEMORY
{
  RAM (rwx) : ORIGIN = 0x00000000, LENGTH = 0x08000000
}

/* The stack lives at the top of that window; start.c loads sp from here. */
__stack_top = 0x08000000;

SECTIONS
{
  .text.boot 0x00000000 : ALIGN(4)
  {
    KEEP(*(.text.boot))
  } > RAM

  /* ... and on the far side of the page the boot ROM owns. */
  .text 0x00000800 : ALIGN(4)
  {
    *(.text .text.*)
    *(.rodata .rodata.*)
  } > RAM

  .data : ALIGN(8)
  {
    *(.data .data.*)
  } > RAM

  .bss (NOLOAD) : ALIGN(8)
  {
    __bss_start = .;
    *(.bss .bss.*)
    *(COMMON)
    __bss_end = .;
  } > RAM

  /DISCARD/ :
  {
    *(.comment)
    *(.note*)
    *(.ARM.attributes)
    *(.ARM.exidx*)
    *(.eh_frame)
  }
}