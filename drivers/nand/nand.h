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
