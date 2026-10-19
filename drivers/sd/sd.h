/*
 * sd.h - the microSD slot.
 */
#ifndef SD_H
#define SD_H

#include "spl.h"

#define SD_BLOCK_SIZE      512u

struct sd_card {
    int present;
    u32 rca;
    u32 csd[4];
    u64 blocks;            /* capacity, in 512-byte blocks */
    int byte_addressed;    /* CSD v1.0 is addressed in bytes, v2.0 in blocks */
};

/*
 * Bring the card up: power it, take it to idle, identify it, read its CSD and
 * work out its capacity.  0 if there is a card and it answered.
 *
 * A slot with nothing in it is not an error here - it is a normal outcome, and
 * a machine with no card is a machine that cannot be flashed from one - so it
 * returns non-zero and leaves `card` saying not present.
 */
int sd_init(struct sd_card *card);

/* One 512-byte block.  0 on success. */
int sd_read_block(const struct sd_card *card, u64 block, void *dst);
int sd_write_block(const struct sd_card *card, u64 block, const void *src);

/*
 * A run of blocks, which is what a filesystem asks for.  Unlike the multi-block
 * commands this is a loop of single ones: the data mover path needs a linked
 * list in RAM and five completion callbacks, and a bootloader that is reading
 * a file has no use for any of that.  Slower, and it does not matter.
 */
int sd_read_blocks(const struct sd_card *card, u64 first, u32 count, void *dst);

#endif /* SD_H */
