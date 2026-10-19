/*
 * boot.h - turning the MMU off and handing the core over.
 */
#ifndef BOOT_H
#define BOOT_H

/*
 * Jump to `addr` with the caches off, the MMU off and the TLB empty.  It does
 * not return.  This is the last thing the bootloader does.
 */
void boot_jump(u32 addr);

#endif /* BOOT_H */
