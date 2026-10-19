/*
 * nand.h - the MSM NAND controller's flash interface.
 */
#ifndef NAND_H
#define NAND_H

#include "spl.h"

/* Geometry of the HD2's part: 4096 x 64 x (2048 + 64), four 528-byte words. */
#define NAND_PAGE_SIZE      2048u
#define NAND_CODEWORDS      4u
#define NAND_CW_USER        512u  /* the user data in one codeword        */

/*
 * Where the bad-block marker lives: two bytes of the last codeword's user
 * area.  They have to read 0xFF for the block to count as good, so the driver
 * writes them as 0xFF whatever the data says, and anything comparing a file
 * against the flash has to allow for it.
 */
#define BAD_BLOCK_OFFSET    464u

/*
 * The spare area: four bytes at the end of each codeword's buffer entry,
 * which the controller puts in the chip's own spare area rather than in the
 * user data.  Raw column 524; offset 522 of an ECC-mode read.
 */
#define NAND_SPARE_OFF      522u
#define NAND_SPARE_LEN      4u
#define NAND_CW_RAW         528u  /* that plus ECC, spare and the marker  */
#define NAND_PAGES_PER_BLK  64u
#define NAND_NUM_BLOCKS     4096u
#define NAND_BLOCK_SIZE     (NAND_PAGES_PER_BLK * NAND_PAGE_SIZE)

struct nand_id {
    u8 maker;
    u8 device;
};

struct nand_info {
    struct nand_id id;
    u32 num_blocks;
    u32 pages_per_block;
};

/*
 * Bring the controller up and read the part's id.  Returns 0 on success,
 * negative on failure.  This is the "NANDFlashIdentify" the SPL does before
 * anything else touches the flash.
 */
int nand_init(struct nand_info *info);

/* Read one 2048-byte page of logical data into `dst`. 0 on success. */
int nand_read_page(u32 page, void *dst);

/* Copy `count` pages from `page` to `dst`. 0 on success. */
int nand_read_pages(u32 page, void *dst, u32 count);

/*
 * Write one 2048-byte page.  Programming a NAND can only clear bits, so a
 * page has to be erased before anything else can go into it.
 */
int nand_write_page(u32 page, const void *src);

/*
 * As nand_write_page(), with the four spare bytes of each codeword given:
 * `spare` points at NAND_CODEWORDS * 4 bytes, or null to leave them erased.
 *
 * This matters for anything but a plain image.  The phone's flash driver
 * keeps WinCE's per-sector information - the sector's number and its tag - in
 * this spare area, and an image flashed without it is readable but has no
 * sectors the OS can find.
 */
int nand_write_page_ex(u32 page, const void *src, const void *spare);

/* Erase one whole block. 0 on success. */
int nand_erase_block(u32 block);

/*
 * Copy a run of pages that skips bad blocks, the way the "OS" area is laid
 * out on a part with holes in it.  Starts at `first_page`, which does not
 * have to be a block boundary: a bad block anywhere behind the cursor is
 * stepped over whole, so the image still lands contiguously.
 * 0 on success.
 */
int nand_read_run(u32 first_page, u32 npages, void *dst);

/* True if the block's first page carries a factory bad-block marker. */
int nand_block_is_bad(u32 block);

#endif /* NAND_H */
