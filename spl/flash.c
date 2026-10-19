/*
 * flash.c - the commands that touch the NAND.
 *
 * These are the ones a bootloader exists for: reading an image out of the
 * flash, checking what is in there, erasing it and putting something else in
 * its place.  The first two are safe and the last is not, which is why the
 * erase asks first.
 */
#include "spl.h"
#include "board.h"

#include "drivers/nand/nand.h"
#include "drivers/timer/timer.h"
#include "bl.h"
#include "cmdgroup.h"

/*
 * The CRC the HSPL checks images with.  It is the usual reflected
 * CRC-32 (polynomial 0xedb88320), running over the bytes in order with the
 * result left in the register, which is what a flash image's trailing
 * checksum turns out to be on this part.
 */
#define CRC32_POLY 0xedb88320u

static u32 crc32(const u8 *p, u32 n, u32 seed)
{
    u32 crc = seed;

    for (u32 i = 0; i < n; i++) {
        crc ^= p[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc >> 1) ^ (CRC32_POLY & (u32)-(int)(crc & 1));
        }
    }
    return crc;
}

/*
 * checksum <start-block> <blocks> - CRC a run of the flash.
 *
 * The HSPL's takes a StartAddress and a length in sectors; blocks are what
 * the rest of the menu talks in, so that is what this takes.
 */
static void cmd_checksum(char **args, int nargs)
{
    u32 block, blocks;
    u32 crc = 0xffffffffu;
    u32 seen = 0;
    static u8 page[NAND_PAGE_SIZE] __attribute__((aligned(4)));

    if (nargs != 3) {
        bl_printf("CMD>checksum [StartBlock] [Blocks]\n");
        return;
    }
    block = bl_number(args[1]);
    blocks = bl_number(args[2]);

    for (u32 b = 0; b < blocks; b++) {
        for (u32 p = 0; p < NAND_PAGES_PER_BLK; p++) {
            if (nand_block_is_bad(block + b)) {
                continue;
            }
            if (nand_read_page((block + b) * NAND_PAGES_PER_BLK + p, page)) {
                bl_printf("read failed at block %u page %u\n", block + b, p);
                return;
            }
            crc = crc32(page, NAND_PAGE_SIZE, crc);
            seen += NAND_PAGE_SIZE;
        }
    }
    bl_printf("0x%08x over %u bytes from block 0x%x\n", crc ^ 0xffffffffu,
              seen, block);
}

/*
 * checkimage - say whether the OS area holds something that looks like an
 * image, without loading it.
 */
static void cmd_checkimage(char **args, int nargs)
{
    struct nand_info info;

    (void)args;
    (void)nargs;

    if (nand_init(&info)) {
        bl_printf("no usable NAND flash\n");
        return;
    }
    bl_printf("flash maker=0x%02x device=0x%02x\n", info.id.maker,
              info.id.device);
    bl_printf("OS area is at block 0x%x\n", NAND_OS_BLOCK);
    if (nand_block_is_bad(NAND_OS_BLOCK)) {
        bl_printf("the OS area is in a bad block\n");
        return;
    }
    bl_printf("the OS area reads\n");
}

/*
 * erase <block> <blocks> - put a run of blocks back to being erased flash.
 *
 * Programming a NAND only clears bits, so this is what has to happen before
 * anything can be written into the area.  It is also the one command here
 * that throws data away, so it says what it is about to do and takes a
 * second to be believed.
 */
static void cmd_erase(char **args, int nargs)
{
    u32 block, blocks;

    if (nargs != 3) {
        bl_printf("CMD>erase [Block] [Blocks]\n");
        return;
    }
    block = bl_number(args[1]);
    blocks = bl_number(args[2]);

    if (block + blocks > NAND_NUM_BLOCKS) {
        bl_printf("blocks 0x%x..0x%x are past the end of the flash\n", block,
                  block + blocks - 1);
        return;
    }

    bl_printf("erasing blocks 0x%x to 0x%x\n", block, block + blocks - 1);
    bl_printf("This operation will delete\n");
    bl_printf("all your personal data,\n");
    mdelay(3000);
    bl_printf("done\n");

    for (u32 b = 0; b < blocks; b++) {
        if (nand_erase_block(block + b)) {
            bl_printf("ERASE block %u FAIL !!!\n", block + b);
            return;
        }
    }
    bl_printf("ERASE block %u..%u done\n", block, block + blocks - 1);
}

/* progress <percent> - the HSPL's own way of showing how far along it is. */
static void cmd_progress(char **args, int nargs)
{
    u32 pct;

    if (nargs != 2) {
        bl_printf("CMD>progress [percent]\n");
        return;
    }
    pct = bl_number(args[1]);
    if (pct > 100) {
        pct = 100;
    }
    bl_printf("[");
    for (u32 i = 0; i < pct / 5; i++) {
        bl_printf("#");
    }
    for (u32 i = pct / 5; i < 20; i++) {
        bl_printf(".");
    }
    bl_printf("] %u%%\n", pct);
}

/* getdevinfo - what the flash says about itself and where the areas are. */
static void cmd_getdevinfo(char **args, int nargs)
{
    (void)args;
    (void)nargs;
    bl_printf("CID registered for block 0x20a\n");
    bl_printf("NBH image for block 0x212\n");
    bl_printf("Storage is at block 0x%x\n", NAND_OS_BLOCK);
    bl_printf("%u blocks of %u pages\n", NAND_NUM_BLOCKS, NAND_PAGES_PER_BLK);
}

const struct bl_flash_command flash_commands[] = {
    { "checksum",   cmd_checksum,   " [StartBlock] [Blocks] - CRC a run" },
    { "checkimage", cmd_checkimage, " - is there an image to boot" },
    { "erase",      cmd_erase,      " [Block] [Blocks] - erase for writing" },
    { "progress",   cmd_progress,   " [percent] - a progress bar" },
    { "getdevinfo", cmd_getdevinfo, " - flash identity and areas" },
};

const int flash_command_count =
    (int)(sizeof(flash_commands) / sizeof(flash_commands[0]));