/*
 * autoboot.h - loading the OS image and handing the core over.
 */
#ifndef AUTOBOOT_H
#define AUTOBOOT_H

/*
 * Find the OS image in the flash, clear where it goes, load it and jump.
 * Does not come back.  Both the normal boot path and the bootloader's "boot"
 * command come through here, so neither has to know how any of it works.
 */
void autoboot(void);

#endif /* AUTOBOOT_H */