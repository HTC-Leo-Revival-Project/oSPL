/*
 * osimage.h - finding and loading the image in the NAND "OS" area.
 */
#ifndef OSIMAGE_H
#define OSIMAGE_H

#include "spl.h"

struct os_image {
    u32 base;       /* where it is loaded                       */
    u32 size;       /* how many bytes came out of the flash      */
    u32 start;      /* first sector of the image inside the area */
    u32 sectors;    /* how many sectors it is                   */
    u32 part;       /* which partition entry described it        */
};

/*
 * Read the OS area's MBR out of the flash, check it is an image we know how
 * to lay out, and report where the image is.  0 on success.
 */
int osimage_locate(struct os_image *img);

/* Copy the image to `img->base`.  0 on success. */
int osimage_load(const struct os_image *img);

/*
 * The other half of flashing: describe an image in partition `part` starting
 * at `start_sector` and `sectors` long, and put the tag in front of it.
 *
 * The MBR has to be written after the image and not before, so that a flash
 * cut off halfway leaves an area that is simply empty rather than one that
 * claims to hold something it does not.
 */
int osimage_write_mbr(u32 part, u32 start_sector, u32 sectors);
int osimage_write_tag(u32 start_sector);

#endif /* OSIMAGE_H */