/*
 * nbh.h - the flashing container the phone takes its OS image in.
 *
 * The file is not the OS image.  It is a signed wrapper around a header table
 * and the image area, and what the flash ends up holding is the area: the
 * part above it is here to be read, not stored.
 *
 * There is no heap underneath this program, so the description is the
 * caller's: put one on the stack or in a static, and pass it to nbh_open().
 */
#ifndef NBH_H
#define NBH_H

#include "spl.h"

/* The signed wrapper: "R000FF", '\n', "XDA-Developers!", then a block list. */
#define NBH_MAGIC          "R000FF"
/* The space is part of it: the field is sixteen bytes and the name is fifteen. */
#define NBH_SIGNATURE      "XDA-Developers! "
#define NBH_HEADER_SIZE    0x17u   /* as far as the block list starts    */

/* How the OS area is laid out inside the joined image. */
#define NBH_SECTOR         2048u   /* one of these is one NAND page      */
#define NBH_SECTOR_INFO    8u      /* after each, in the file only       */
#define NBH_STRIDE         (NBH_SECTOR + NBH_SECTOR_INFO)
#define NBH_OS_OFF         0x200u  /* and the area starts here           */

/* Why nbh_open() would not. */
#define NBH_OK             0
#define NBH_E_MAGIC        -1      /* not one of these at all            */
#define NBH_E_SIGNATURE    -2      /* right magic, wrong signature       */
#define NBH_E_TRUNCATED    -3      /* it says more than arrived           */
#define NBH_E_NOMEM        -4      /* the area is larger than the flash   */

#define NBH_MAX_BLOCKS     8

struct nbh_block {
    u32 off;        /* where the data starts, in the file           */
    u32 len;        /* how long it is                               */
    u32 image_off;  /* where that lands once the blocks are joined  */
};

struct nbh {
    u32 base;               /* the file, in RAM                          */
    u32 file_len;           /* how much of it arrived                     */
    u32 block_count;
    struct nbh_block block[NBH_MAX_BLOCKS];

    u32 image_len;          /* joined, header and all                     */
    u32 os_off;             /* the OS area within it                      */
    u32 os_bytes;           /* its length, sector info included           */
    u32 os_sectors;         /* that, in sectors                           */
    u32 os_flash_bytes;     /* and that, without the sector info          */

    u32 target;             /* where the image goes, from the "PB..." text */
    u32 start_sector;       /* out of the area's MBR                      */
    u32 part_sectors;       /* and how many sectors of image              */
    int mbr_part;           /* which partition entry described it         */

    char version[16];       /* "CLK1.4.0.2"                               */
    char build[8];          /* "WWE"                                      */
};

/*
 * Read the file at `addr`, of which `avail` bytes have arrived, into `nb`.
 * On success nbh_flash() can program it.  On failure the reason is put in
 * `*why` and nothing in `nb` is to be believed.
 */
int nbh_open(u32 addr, u32 avail, struct nbh *nb, const char **why);

/*
 * `len` bytes of the joined image from `off`.  Returns how many were copied,
 * which is short at the end of what arrived rather than reading past it.
 */
u32 nbh_image(const struct nbh *nb, u32 off, void *dst, u32 len);

/* One of the four-byte names in the table; the first two are the board. */
const char *nbh_name(const struct nbh *nb, int i, char *dst, u32 len);

/*
 * Program the area into the flash, replacing whatever is there.  The area
 * carries its own MBR, so this writes it verbatim rather than describing it
 * again.
 */
int nbh_flash(const struct nbh *nb, int verbose);

/* Checksums of the area in the flash and in the file, to compare the two. */
u32 nbh_flash_checksum(const struct nbh *nb);
u32 nbh_file_checksum(const struct nbh *nb);

#endif /* NBH_H */
