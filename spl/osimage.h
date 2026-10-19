/*
 * osimage.h - finding and loading the OS image out of the NAND.
 */
#ifndef OSIMAGE_H
#define OSIMAGE_H

#include "spl.h"

struct os_image {
    u32 base;       /* where it is loaded                       */
    u32 size;       /* how many bytes came out of the flash      */
    u32 start;      /* first sector of the image inside the area */
    u32 sectors;    /* how many sectors it is                   */
};

/*
 * Read the "OS" area's MBR out of the flash, check it is an image we know
 * how to lay out, and report where the image is.  0 on success.
 */
int osimage_locate(struct os_image *img);

/* Copy the image to `img->base`.  0 on success. */
int osimage_load(const struct os_image *img);

#endif /* OSIMAGE_H */
