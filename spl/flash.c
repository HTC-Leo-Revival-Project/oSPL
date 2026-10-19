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
#include "drivers/nbh/nbh.h"
#include "drivers/timer/timer.h"
#include "drivers/uart/uart.h"
#include "osimage.h"
#include "bl.h"
#include "cmdgroup.h"
#include "crc32.h"

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

/*
 * getdevinfo - what the flash says about itself and where the areas are.
 *
 * The areas are named after what this one knows them to be.  Where an NBH
 * says so, that is better than a constant here, so the block the OS area
 * lives in is taken from the area's own MBR and the block the HSPL lives in
 * stays a constant - the HSPL is loaded by the boot ROM from a block this
 * code is not the one choosing.
 */
static void cmd_getdevinfo(char **args, int nargs)
{
    struct os_image img;

    (void)args;
    (void)nargs;
    bl_printf("CID registered for block 0x%x\n", NAND_SPL_BLOCK);
    bl_printf("NBH image for block 0x%x\n", NAND_OS_BLOCK);
    bl_printf("Storage is at block 0x%x\n", NAND_OS_BLOCK);
    bl_printf("%u blocks of %u pages\n", NAND_NUM_BLOCKS, NAND_PAGES_PER_BLK);

    if (!osimage_locate(&img)) {
        bl_printf("OS area: partition %u, %u sectors from %u, at 0x%08x\n",
                  img.part, img.sectors, img.start, (u32)img.base);
    }
}

/*
 * recv <address> <length> - take `length` bytes in over the console.
 *
 * This is how a file gets into RAM.  There is no filesystem and no download
 * protocol underneath this, so the bytes come down the same serial line the
 * menu is on, and the person on the other end is the one sending them.
 *
 * It stops on a timeout rather than hanging: a receive that is waiting for
 * bytes that are not coming should leave the bootloader able to say so and
 * come back to the prompt, not wedge it.
 */
static void cmd_recv(char **args, int nargs)
{
    u32 addr, len, got = 0;
    u8 *dst;

    if (nargs != 3) {
        bl_printf("CMD>recv [Address] [Length]\n");
        return;
    }
    addr = bl_number(args[1]);
    len = bl_number(args[2]);
    if (!len || len > DOWNLOAD_MAX) {
        bl_printf("length %u does not fit in the download area\n", len);
        return;
    }
    if (addr & 3) {
        bl_printf("address 0x%x is not word aligned\n", addr);
        return;
    }

    bl_printf("recv: %u bytes to 0x%08x, send them now\r\n", len, addr);
    dst = (u8 *)(u32)addr;

    while (got < len) {
        int c = uart_getc();
        int idle = 0;

        while (c < 0) {
            udelay(1000);
            if (++idle > 5000) {        /* five seconds and nothing */
                bl_printf("\r\nrecv: gave up after %u of %u bytes\r\n",
                          got, len);
                return;
            }
            c = uart_getc();
        }
        dst[got++] = (u8)c;

        /*
         * Progress is reported sparingly and over the top of itself.  Every
         * character sent here has to be read by the other end before this
         * loop can take another byte, so a bootloader that says too much
         * while receiving makes the receiving slower - and an image is 128 KB
         * enough times over.
         */
        if (got == len || !(got & 0x7fff)) {
            bl_printf("\r\nrecv: %u/%u\r", got, len);
        }
    }
    bl_printf("\r\nrecv: %u bytes\r\n", got);
}

/*
 * nbhinfo [address] [length] - what the file at `address` says.
 *
 * The length is needed because the file is not self-terminating as far as a
 * reader is concerned: the block list ends with a length of zero, but how
 * much of it arrived is something the sender knows and the receiver does not.
 */
static int nbh_open_at(char **args, int nargs, struct nbh *nb,
                       u32 *addr_out)
{
    u32 addr = DOWNLOAD_BASE;
    u32 len = DOWNLOAD_MAX;
    const char *why = NULL;
    int r;

    if (nargs > 1 && args[1][0]) {
        addr = bl_number(args[1]);
    }
    if (nargs > 2 && args[2][0]) {
        len = bl_number(args[2]);
    }

    r = nbh_open(addr, len, nb, &why);
    if (r) {
        bl_printf("nbh: %s\r\n", why ? why : "no");
        bl_printf("nbh: LEOIMG.nbh - FAIL\r\n");
        return r;
    }
    if (addr_out) {
        *addr_out = addr;
    }
    return 0;
}

static void cmd_nbhinfo(char **args, int nargs)
{
    struct nbh nb;

    if (nbh_open_at(args, nargs, &nb, NULL)) {
        return;
    }
    bl_printf("blocks        %u, %u bytes joined\r\n", nb.block_count,
              nb.image_len);
    bl_printf("version       %s  build %s\r\n", nb.version, nb.build);
    bl_printf("target        0x%x\r\n", nb.target);
    bl_printf("OS area       +0x%x, %u bytes in %u sectors\r\n", nb.os_off,
              nb.os_bytes, nb.os_sectors);
    bl_printf("              (%u bytes of it are sector info)\r\n",
              nb.os_sectors * NBH_SECTOR_INFO);
    bl_printf("MBR partition %u: %u sectors from %u\r\n", nb.mbr_part,
              nb.part_sectors, nb.start_sector);
    for (int i = 0; i < 6; i++) {
        char name[8];

        if (nbh_name(&nb, i, name, sizeof(name)) && name[0]) {
            bl_printf("name %d        \"%s\"%s\r\n", i, name,
                      i < 2 ? "  (board)" : "");
        }
    }
    bl_printf("nbh: LEOIMG.nbh - OK\r\n");
}

/*
 * nbhsections [address] [length] - the section table, and where each section
 * would go.
 *
 * A stock ROM has eight sections and this can place one of them, so this says
 * which is which rather than presenting a file as though it were a single
 * image.  A section with no known destination is listed as one, not guessed
 * at: a partition written to the wrong block is a radio that does not come
 * up, and there is no undo short of another flash.
 */
static void cmd_nbhsections(char **args, int nargs)
{
    struct nbh nb;

    if (nbh_open_at(args, nargs, &nb, NULL)) {
        return;
    }
    bl_printf("sections %u, %u bytes joined\r\n", nb.section_count,
              nb.image_len);
    for (u32 i = 0; i < nb.section_count; i++) {
        const struct nbh_section *sec = &nb.section[i];
        const char *where;

        switch (sec->type) {
        case NBH_SEC_OS:     where = "block 0x212, the OS area";   break;
        case NBH_SEC_RADIO:  where = "the radio's own table";     break;
        case NBH_SEC_LOADER: where = "not worked out";            break;
        default:             where = "not worked out";            break;
        }
        bl_printf("  0x%03x  +0x%08x  %10u bytes  %s\r\n", sec->type,
                  sec->off, sec->len, where);
    }
}

/*
 * nbhflash [address] [length] - program the area the file describes.
 *
 * This is the destructive one, so it asks first: the block it erases holds the
 * image the phone boots, and there is no putting that back.
 */
static void cmd_nbhflash(char **args, int nargs)
{
    struct nbh nb;
    u32 addr = DOWNLOAD_BASE;
    u32 file_crc, flash_crc;

    if (nbh_open_at(args, nargs, &nb, &addr)) {
        return;
    }
    bl_printf("nbhflash: %s build %s, %u sections\r\n", nb.version,
              nb.build, nb.section_count);
    bl_printf("nbhflash: OS is %u sectors from block 0x%x\r\n",
              nb.os_sectors, (u32)NAND_OS_BLOCK);
    bl_printf("nbhflash: this erases block 0x%x.  type 'yes' to go\r\n",
              (u32)NAND_OS_BLOCK);

    {
        int answered = 0;

        while (!answered) {
            int c;
            int idle = 0;

            bl_printf("nbhflash> ");
            while ((c = uart_getc()) < 0) {
                udelay(1000);
                if (++idle > 30000) {       /* thirty seconds to answer */
                    bl_printf("\r\nnbhflash: nobody said yes, so not\r\n");
                    return;
                }
            }
            if (c == '\r' || c == '\n') {
                continue;
            }
            if (c == 'y' || c == 'Y') {
                answered = 1;
            } else if (c == 'n' || c == 'N' || c == 0x03) {
                bl_printf("\r\nnbhflash: not done\r\n");
                return;
            } else {
                /* anything else is not an answer; ignore it */
                continue;
            }

            /*
             * Take the rest of the line.  Whoever typed "yes" meant it, and
             * leaving "es" on the line has it read as the next command, which
             * is both wrong and confusing to look at afterwards.
             */
            idle = 0;
            while ((c = uart_getc()) < 0 && idle < 3000) {
                udelay(1000);
                idle++;
            }
            if (c >= 0) {
                while (c != '\r' && c != '\n' && idle < 3000) {
                    idle = 0;
                    while ((c = uart_getc()) < 0 && idle < 3000) {
                        udelay(1000);
                        idle++;
                    }
                }
            }
        }
    }

    file_crc = nbh_file_checksum(&nb);
    if (nbh_flash_all(&nb, 1)) {
        return;
    }
    flash_crc = nbh_flash_checksum(&nb);

    bl_printf("nbhflash: file 0x%08x, flash 0x%08x\r\n", file_crc, flash_crc);
    if (file_crc != flash_crc) {
        bl_printf("nbhflash: they differ - the write did not take\r\n");
        return;
    }
    bl_printf("nbhflash: verified\r\n");

    /* and the flash has to read back as an image we can boot */
    {
        struct os_image img;

        if (osimage_locate(&img)) {
            bl_printf("nbhflash: but there is still no image to boot\r\n");
            return;
        }
        bl_printf("nbhflash: image at 0x%08x, %u sectors - boot it\r\n",
                  (u32)img.base, img.sectors);
    }
}

const struct bl_flash_command flash_commands[] = {
    { "checksum",   cmd_checksum,   " [StartBlock] [Blocks] - CRC a run" },
    { "checkimage", cmd_checkimage, " - is there an image to boot" },
    { "erase",      cmd_erase,      " [Block] [Blocks] - erase for writing" },
    { "progress",   cmd_progress,   " [percent] - a progress bar" },
    { "getdevinfo", cmd_getdevinfo, " - flash identity and areas" },
    { "recv",       cmd_recv,       " [Addr] [Length] - take bytes in" },
    { "nbhinfo",    cmd_nbhinfo,    " [Addr] [Length] - what the file says" },
    { "nbhflash",   cmd_nbhflash,   " [Addr] [Length] - flash it" },
    { "nbhsections", cmd_nbhsections, " [Addr] [Length] - the section table" },
};

const int flash_command_count =
    (int)(sizeof(flash_commands) / sizeof(flash_commands[0]));