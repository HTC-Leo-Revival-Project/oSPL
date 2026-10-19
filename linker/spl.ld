/*
 * spl.ld - the image the boot ROM copies to physical 0.
 *
 * The ROM jumps to offset 0, so .text.boot has to be the first thing in the
 * image.  Everything runs from SDRAM with the MMU off; there is no room
 * reserved for the stack because start.s picks one in board.h.
 */
ENTRY(_start)

MEMORY
{
  RAM (rwx) : ORIGIN = 0x00000000, LENGTH = 0x08000000
}

/* The stack lives at the top of that window; start.s loads sp from here. */
__stack_top = 0x08000000;

SECTIONS
{
  .text : ALIGN(4)
  {
    KEEP(*(.text.boot))
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
