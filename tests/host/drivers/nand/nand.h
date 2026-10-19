/*
 * nand.h - the host stand-in.  There is no flash here; see stub.c.
 *
 * The geometry is the real one, because the NBH driver does arithmetic on it:
 * the area is a whole number of pages, and two bytes of every page belong to
 * the part's bad-block marker rather than to the data.
 */
#ifndef HOST_NAND_H
#define HOST_NAND_H

#include "spl.h"

#define NAND_PAGE_SIZE      2048u
#define NAND_PAGES_PER_BLK  64u
#define NAND_BLOCK_SIZE     (NAND_PAGES_PER_BLK * NAND_PAGE_SIZE)
#define NAND_NUM_BLOCKS     4096u

#define NAND_CODEWORDS      4u
#define NAND_CW_USER        512u
#define NAND_CW_RAW         528u
#define BAD_BLOCK_OFFSET    464u

int nand_read_page(u32 page, void *dst);
int nand_write_page(u32 page, const void *src);
int nand_erase_block(u32 block);

#endif
