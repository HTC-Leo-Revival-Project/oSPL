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
#define NBH_E_SIGNATURE    -2      /* reserved: the signature is not
                                    * required, so this is never returned  */
#define NBH_E_TRUNCATED    -3      /* it says more than arrived           */
#define NBH_E_NOMEM        -4      /* the area is larger than the flash   */
#define NBH_E_NO_OS        -5      /* a radio-only flash: no OS section   */

/*
 * The block list is regular in every file seen: 0x10000 bytes of data behind
 * a 9-byte header, then 0x80 bytes of signature, over and over.  A stock ROM
 * has 4370 of them and a cCLK NBH three, so the list is not held - the
 * stride is, and the offset of a byte is arithmetic.  NBH_MAX_BLOCKS only
 * bounds the fallback for a list that is *not* regular, which is a different
 * thing and is checked for rather than assumed.
 */
#define NBH_MAX_BLOCKS     8

/*
 * The section table.
 *
 * Up to NBH_MAX_SECTIONS of them, each a type, where it is in the joined
 * image and how long it is.  The types are HTC's, not ours, and they are what
 * say where each piece of the ROM goes:
 *
 *   0x400  the OS - a WinCE area: an MBR, the ULDR, the XIP kernel, the
 *          IMGFS and room for storage.  This is the only section a phone
 *          flashing a single OS cares about, and the only one cCLK's NBH has
 *          at all.
 *   0x301  the radio: the boot area, FSBL, OSBL, AMSS and the DSP, with a
 *          partition table of their own inside it naming the blocks.
 *   0x200  512 KiB of code starting with a branch - the loader that runs
 *          before the OS does.
 *   0x380, 0x600, 0x920, 0x610, 0x611  the modem's identity and config, the
 *          splash, and two more whose contents this has not identified.
 *
 * A file with one section is a single OS image; a stock NBH has all of them,
 * and flashing it means every one of them going somewhere.
 */
#define NBH_MAX_SECTIONS    32

#define NBH_SEC_OS          0x400u
#define NBH_SEC_RADIO       0x301u
#define NBH_SEC_LOADER      0x200u

struct nbh_section {
    u32 type;
    u32 off;        /* where it starts in the joined image */
    u32 len;
};

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

    u32 section_count;
    struct nbh_section section[NBH_MAX_SECTIONS];

    int signed_by_xda;   /* whether the signature was there; it is optional */

    /*
     * Set when every block in the list was the same size, which is every file
     * seen.  Then a byte of the joined image is at a fixed stride from the
     * start of the file and no per-block table is needed - a stock ROM is
     * 287 MB and holding an entry for each of 4370 blocks to say the same
     * thing is a waste of the one thing a bootloader has least of.
     */
    u32 block_len;       /* bytes of data per block      */
    u32 block_span;      /* and the whole stride, header and signature too */
    u32 block_seam;      /* signature bytes behind each block             */
    int irregular;       /* the block sizes change part way through, so the
                         * stride does not describe the file and the small
                         * table is what is read instead                    */
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

/* The nth section, or null if the file has not got that many. */
const struct nbh_section *nbh_section(const struct nbh *nb, u32 n);

/* The section of a given type, or null. */
const struct nbh_section *nbh_find(const struct nbh *nb, u32 type);

/*
 * Program the area into the flash, replacing whatever is there.  The area
 * carries its own MBR, so this writes it verbatim rather than describing it
 * again.
 */
int nbh_flash(const struct nbh *nb, int verbose);

/*
 * Write every section that has somewhere to go, and report the ones that do
 * not rather than guessing where they belong.
 *
 * The OS goes to the area at block 0x212; the radio and the rest carry
 * destinations of their own that are not worked out yet, and are listed
 * instead of written.  This never erases the block the bootloader is in, so
 * a file that describes that block cannot take the bootloader out from under
 * itself mid-flash.
 */
int nbh_flash_all(const struct nbh *nb, int verbose);

/* Checksums of the area in the flash and in the file, to compare the two. */
u32 nbh_flash_checksum(const struct nbh *nb);
u32 nbh_file_checksum(const struct nbh *nb);

#endif /* NBH_H */
