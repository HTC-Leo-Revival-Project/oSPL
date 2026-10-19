/*
 * nbh.c - the NBH, which is how this phone is flashed.
 *
 * The file is not an image.  It is a wrapper that says what the image is,
 * signed in blocks so that a half-received file cannot be mistaken for a whole
 * one:
 *
 *   0x00  "R000FF"                 six bytes of magic
 *   0x06  '\n'
 *   0x07  "XDA-Developers! "       sixteen bytes, trailing space and all
 *   0x17  a block list, until one with a length of zero:
 *             u32  length          bytes of data
 *             u32  siglen          bytes of signature that follow it
 *             u8   flag            1: the data is signed
 *             u8   data[length]
 *             u8   sig[siglen]
 *
 * What the blocks hold, once they are joined back together, is this:
 *
 *   0x000   the board and partition names, four bytes each: the board is
 *           "HT" in the first eight, and the partitions after it
 *   0x020   where the image is to go, as text: "PB8110000"
 *   0x140   how many bytes of the OS area follow the header
 *   0x1e0   the version, as text: "CLK1.4.0.2"
 *   0x1f0   the build tag: "WWE"
 *   0x200   the OS area, one 2048-byte sector per NAND page and 8 bytes of
 *           sector info after each - the number of the sector and two bytes
 *           that repeat the MBR's signature
 *
 * That last part is the only one the flash actually keeps.  The area carries
 * its own MBR, so nothing above offset 0x200 needs writing: it is here to be
 * read, not stored.  A whole 0x10000-byte block goes in the file for every
 * 0x10000 bytes of image, so a 0x20400-byte image arrives as two full blocks
 * and one of 0x400.
 */
#include "spl.h"
#include "board.h"
#include "nand.h"
#include "nbh.h"
#include "crc32.h"
#include "drivers/uart/uart.h"

/* The signed wrapper. */
#define NBH_MAGIC          "R000FF"
#define NBH_MAGIC_LEN      6
#define NBH_SIG_OFF        0x07
#define NBH_SIG_LEN        16      /* and the name is fifteen */
#define NBH_SIG            "XDA-Developers! "  /* sixteen, space and all */
#define NBH_LIST_OFF       0x17
#define NBH_BLOCK_HDR      9       /* length, signature length, flag */

/* The joined image. */
#define NBH_TABLE_OFF      0x000   /* names and where to put the image */
#define NBH_NAMES_OFF      0x000
#define NBH_NAME_LEN       4
#define NBH_TARGET_OFF     0x020   /* "PB8110000"                      */
#define NBH_VERSION_OFF    0x1e0
#define NBH_VERSION_LEN    16
#define NBH_BUILD_OFF      0x1f0
#define NBH_BUILD_LEN      8

/*
 * The section table: 32 types, 32 offsets and 32 lengths, each array a
 * contiguous run of words.  They line up with each other by index, which is
 * the whole of the format's cleverness - there is no count and no terminator,
 * just three arrays that stop being non-zero.
 *
 * The board and partition names are four bytes each from the front, and the
 * target address is written out as text at 0x20.  Everything before 0x40 is
 * that; the tables start where they are because the device field is twice as
 * wide here as it is in HTCFlasher's own generator, which is the only
 * difference between the two layouts.
 */
#define NBH_TYPES_OFF      0x040
#define NBH_OFFSETS_OFF    0x0c0
#define NBH_LENGTHS_OFF    0x140
#define NBH_HDR_SIZE       0x200   /* and the sections start here       */

/* How the OS area is laid out inside the file. */
#define NBH_SECTOR         2048u
#define NBH_SECTOR_INFO    8u
#define NBH_STRIDE         (NBH_SECTOR + NBH_SECTOR_INFO)

#define CRC_SEED   0xffffffffu

/*
 * Copy `len` bytes out of the joined image starting at `off`.
 *
 * The bytes are not contiguous in the file - every block has a header in
 * front of it and a signature behind - so this has to walk the blocks.  It
 * stops at the end of what arrived and reports how much it got, which is
 * how a short file is caught rather than read past.
 */
u32 nbh_image(const struct nbh *nb, u32 off, void *dst, u32 len)
{
    u8 *out = dst;
    u32 got = 0;

    if (nb->block_len) {
        /*
         * The regular case: every block is the same, so a byte's place in
         * the file is its place in the image plus a fixed number of strides.
         * This is what lets one routine read a 287 MB ROM it could never hold.
         */
        u32 blk = off / nb->block_len;
        u32 skip = off - blk * nb->block_len;
        u64 at = NBH_LIST_OFF + NBH_BLOCK_HDR + (u64)blk * nb->block_span + skip;
        u32 take;

        if (skip >= nb->block_len || at >= (u64)nb->file_len) {
            return 0;
        }
        take = len;
        if ((u64)at + take > nb->file_len) {
            take = (u32)((u64)nb->file_len - at);
        }
        if (off + take > nb->image_len) {
            if (off >= nb->image_len) {
                return 0;
            }
            take = nb->image_len - off;
        }
        if (!take) {
            return 0;
        }
        memcpy(dst, (const void *)(u32)(nb->base + (u32)at), take);
        return take;
    }

    while (got < len) {
        const struct nbh_block *b = NULL;
        u32 i;
        u32 skip;

        for (i = 0; i < nb->block_count; i++) {
            if (off >= nb->block[i].image_off &&
                off < nb->block[i].image_off + nb->block[i].len) {
                b = &nb->block[i];
                break;
            }
        }
        if (!b) {
            break;                  /* past the end of what arrived */
        }
        skip = off - b->image_off;

        {
            u32 avail = b->len - skip;
            u32 take = len - got;

            if (take > avail) {
                take = avail;
            }
            /*
             * b->off is an offset into the file and the base is added
             * here, once.  Recording it already absolutised and adding the
             * base again is how the first version of this read from twice
             * the address the file was received at.
             */
            memcpy(out + got, (const void *)(u32)(nb->base + b->off + skip),
                   take);
            got += take;
            off += take;
        }
    }
    return got;
}

static u32 le32(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) |
           ((u32)p[3] << 24);
}

static void image_copy(const struct nbh *nb, u32 off, char *dst, u32 len)
{
    if (nbh_image(nb, off, dst, len) != len) {
        memset(dst, 0, len);
    }
    dst[len - 1] = '\0';            /* it is a null-padded string */
}

/*
 * "PB8110000" is an address written out in hexadecimal with a two-letter
 * prefix.  Read it the way the file says it rather than guessing a width.
 */
static u32 target_of(const char *s)
{
    u32 v = 0;
    int digits = 0;

    while (*s >= '0' && *s <= '9') {
        v = v * 16 + (u32)(*s - '0');
        s++;
        digits++;
    }
    while (*s >= 'a' && *s <= 'f') {
        v = v * 16 + (u32)(*s - 'a' + 10);
        s++;
        digits++;
    }
    while (*s >= 'A' && *s <= 'F') {
        v = v * 16 + (u32)(*s - 'A' + 10);
        s++;
        digits++;
    }
    return digits ? v : 0;
}

int nbh_open(u32 addr, u32 avail, struct nbh *nb, const char **why)
{
    const u8 *f = (const u8 *)(u32)addr;
    u32 off = NBH_LIST_OFF;
    u32 image_len = 0;
    static u8 buf[512] __attribute__((aligned(4)));

    *why = NULL;

    if (avail < NBH_LIST_OFF) {
        *why = "not enough of it arrived to hold a header";
        return NBH_E_TRUNCATED;
    }
    if (memcmp(f, NBH_MAGIC, NBH_MAGIC_LEN)) {
        *why = "no R000FF at the front of it";
        return NBH_E_MAGIC;
    }
    /*
     * The signature is not required.  HTCFlasher's generator writes
     * "XDA-Developers! " there and HTC's own tool leaves sixteen zero bytes,
     * and both produce files that flash, so it says who wrote the file rather
     * than what it is.  What decides is the structure: the block list has to
     * walk cleanly and the section table has to add up, and both are checked
     * below.  Refusing a file over a field that is optional is refusing a
     * working flash because of a signature.
     */
    memset(nb, 0, sizeof(*nb));
    nb->signed_by_xda = memcmp(f + NBH_SIG_OFF, NBH_SIG, NBH_SIG_LEN) == 0;
    nb->base = addr;
    nb->file_len = avail;

    /*
     * The block list.
     *
     * Every block seen is 0x10000 bytes of data behind a 9-byte header with
     * 0x80 bytes of signature behind that, and a stock ROM has 4370 of them -
     * so what is kept is the stride, and a byte's place in the file is its
     * place in the joined image plus a fixed number of strides.  That is what
     * lets one routine read a 287 MB image it could never hold.
     *
     * A list that changes size part way through would need a table, and there
     * is room for a handful; past that it is refused rather than half read,
     * because a file that cannot be addressed exactly is a file that would be
     * flashed to the wrong place.
     */
    while (off + NBH_BLOCK_HDR <= avail) {
        u8 hdr[NBH_BLOCK_HDR];
        u32 len, siglen, data;

        memcpy(hdr, f + off, NBH_BLOCK_HDR);
        len = (u32)hdr[0] | ((u32)hdr[1] << 8) | ((u32)hdr[2] << 16) |
              ((u32)hdr[3] << 24);
        siglen = (u32)hdr[4] | ((u32)hdr[5] << 8) | ((u32)hdr[6] << 16) |
                 ((u32)hdr[7] << 24);

        if (!len) {
            break;                  /* the end of the list */
        }
        data = off + NBH_BLOCK_HDR;
        if (data + len + siglen > avail) {
            *why = "the last block runs past what arrived";
            return NBH_E_TRUNCATED;
        }

        if (!nb->block_count) {
            /* the first block sets the stride for every one after it */
            nb->block_len = len;
            nb->block_span = NBH_BLOCK_HDR + len + siglen;
            nb->block_seam = siglen;
            nb->block[0].off = data;
            nb->block[0].len = len;
            nb->block[0].image_off = 0;
            nb->block_count = 1;
        } else if (len != nb->block_len || siglen != nb->block_seam) {
            /*
             * The last block is short - a stock ROM's is 0x5e44 rather than
             * 0x10000, because the image is not a whole number of 64 KB - and
             * that is not the same thing as a list that changes size part way
             * through.  The stride still describes where every byte is; only
             * the final block runs out earlier, and every read is already
             * clamped to the length of the joined image, so it comes out
             * right.
             *
             * So the question is whether this is the block before the
             * terminator.  If it is, the stride stands; if anything follows
             * it, the arithmetic no longer describes the file and it is
             * refused rather than read to the wrong place.
             */
            u32 next = data + len + siglen;

            if (next + NBH_BLOCK_HDR <= avail) {
                const u8 *n = f + next;
                u32 nlen = (u32)n[0] | ((u32)n[1] << 8) | ((u32)n[2] << 16) |
                           ((u32)n[3] << 24);

                if (nlen) {
                    *why = "the block sizes change part way through, "
                           "which this cannot address";
                    return NBH_E_MAGIC;
                }
            }
        }

        image_len += len;
        off = data + len + siglen;
    }

    if (!nb->block_count) {
        *why = "no blocks in it";
        return NBH_E_MAGIC;
    }
    nb->image_len = image_len;

    /*
     * The section table, before anything else: everything else is a section,
     * and a file whose table does not add up is not worth reading further.
     */
    {
        u8 types[NBH_MAX_SECTIONS * 4];
        u8 offs[NBH_MAX_SECTIONS * 4];
        u8 lens[NBH_MAX_SECTIONS * 4];

        if (nbh_image(nb, NBH_TYPES_OFF, types, sizeof(types)) !=
                sizeof(types) ||
            nbh_image(nb, NBH_OFFSETS_OFF, offs, sizeof(offs)) !=
                sizeof(offs) ||
            nbh_image(nb, NBH_LENGTHS_OFF, lens, sizeof(lens)) !=
                sizeof(lens)) {
            *why = "the section table runs past what arrived";
            return NBH_E_TRUNCATED;
        }
        for (u32 i = 0; i < NBH_MAX_SECTIONS; i++) {
            u32 t = le32(types + i * 4);
            u32 o = le32(offs + i * 4);
            u32 l = le32(lens + i * 4);

            if (!t) {
                continue;
            }
            if ((u64)o + l > nb->image_len) {
                *why = "a section runs past the end of the image";
                return NBH_E_MAGIC;
            }
            nb->section[nb->section_count].type = t;
            nb->section[nb->section_count].off = o;
            nb->section[nb->section_count].len = l;
            nb->section_count++;
        }
        if (!nb->section_count) {
            *why = "no sections in it";
            return NBH_E_MAGIC;
        }
    }

    /*
     * The OS section is the one this can place on its own: a WinCE area laid
     * out one page per sector, with 8 bytes of sector info behind each.  A
     * file without one is not refused outright - the radio is flashed from
     * its own file - but there is nothing here that could be booted.
     */
    {
        const struct nbh_section *sec = nbh_find(nb, NBH_SEC_OS);

        if (!sec) {
            /*
             * Not refused so much as noticed: a radio RUU flashes this same
             * container with no OS in it, and there is a real 25 MB one in
             * the tree.  It has parsed correctly - it simply is not a phone
             * image, and saying so is more use than an error.
             */
            *why = "no OS section (type 0x400) - this is a radio flash";
            return NBH_E_NO_OS;
        }
        nb->os_off = sec->off;
        nb->os_bytes = sec->len;
    }
    if (nb->os_bytes < NBH_STRIDE) {
        *why = "the area it describes is smaller than one sector";
        return NBH_E_MAGIC;
    }
    nb->os_sectors = nb->os_bytes / NBH_STRIDE;
    nb->os_flash_bytes = nb->os_sectors * NBH_SECTOR;

    /* the MBR, and the partition inside it that names the image */
    if (nbh_image(nb, nb->os_off, buf, 512) != 512) {
        *why = "the MBR is past the end of what arrived";
        return NBH_E_TRUNCATED;
    }
    if (buf[0] != 0xe9 || buf[1] != 0xfd || buf[510] != 0x55 ||
        buf[511] != 0xaa) {
        *why = "no WinCE MBR at the front of the area";
        return NBH_E_MAGIC;
    }

    nb->mbr_part = -1;
    for (int i = 0; i < 4; i++) {
        const u8 *e = buf + 0x1be + 16 * (u32)i;

        if (e[4] == 0x23) {
            nb->mbr_part = i;
            break;
        }
    }
    if (nb->mbr_part < 0) {
        *why = "the MBR has no Windows Mobile partition";
        return NBH_E_MAGIC;
    }
    {
        /*
         * Through read_le32(), and not by putting four bytes together by
         * hand: the compiler recognises that idiom and folds it straight back
         * into one 32-bit load, and the start of a partition is at 0x1c6 -
         * two bytes into a word.  With translation off the default memory
         * type is Device, which does not do unaligned transfers, so that load
         * is a data abort rather than a read.
         *
         * The phone's own bootloader reads these a byte at a time, which is
         * not a coincidence: it has to.
         */
        const u8 *e = buf + 0x1be + 16 * (u32)nb->mbr_part;

        nb->start_sector = read_le32(e + 8);
        nb->part_sectors = read_le32(e + 12);
    }
    if (!nb->start_sector || !nb->part_sectors) {
        *why = "the partition in the MBR is empty";
        return NBH_E_MAGIC;
    }
    /* the image has to fit in the area it was found in */
    if ((u64)nb->start_sector + nb->part_sectors > nb->os_sectors) {
        *why = "the image is longer than the area around it";
        return NBH_E_MAGIC;
    }
    /*
     * And it has to fit in the flash from the block the area starts at.  A
     * single-OS image is one block; a stock ROM's is thousands, which is the
     * whole of the rest of the part.
     */
    if ((u64)nb->os_flash_bytes >
        (u64)(NAND_NUM_BLOCKS - NAND_OS_BLOCK) * NAND_BLOCK_SIZE) {
        *why = "the area is larger than the flash behind it";
        return NBH_E_MAGIC;
    }

    /*
     * The board's own names, which are only worth reading once it is known to
     * be a real one.  The target is text - "PB8110000" - so it is read as text
     * rather than as the number it looks like.
     */
    {
        char target[16];

        image_copy(nb, NBH_TARGET_OFF, target, sizeof(target));
        nb->target = target_of(target + 2);   /* past the "PB" */
    }
    image_copy(nb, NBH_VERSION_OFF, nb->version, NBH_VERSION_LEN);
    image_copy(nb, NBH_BUILD_OFF, nb->build, NBH_BUILD_LEN);

    return NBH_OK;
}

const struct nbh_section *nbh_section(const struct nbh *nb, u32 n)
{
    return n < nb->section_count ? &nb->section[n] : NULL;
}

const struct nbh_section *nbh_find(const struct nbh *nb, u32 type)
{
    for (u32 i = 0; i < nb->section_count; i++) {
        if (nb->section[i].type == type) {
            return &nb->section[i];
        }
    }
    return NULL;
}

const char *nbh_name(const struct nbh *nb, int i, char *dst, u32 len)
{
    /*
     * The names are four bytes each, null-padded, from the front of the table:
     * the first two are the board ("HT"), and the partitions follow.
     */
    if (i < 0 || (u32)i * NBH_NAME_LEN + NBH_NAME_LEN > NBH_TABLE_OFF + 0x40) {
        dst[0] = '\0';
        return NULL;
    }
    image_copy(nb, NBH_NAMES_OFF + (u32)i * NBH_NAME_LEN, dst, len);
    return dst;
}

/*
 * Write the area into the flash.
 *
 * The block is erased first and in one go: a NAND can only clear bits, so
 * putting a new area over an old one without erasing it would leave the two
 * OR'd together.  The whole block is erased even though only some of it is
 * written, because a power cut part way through then leaves an area whose
 * MBR is missing - which osimage_locate() reports as no image - rather than
 * an MBR that points at sectors of the old one.
 */
/*
 * Erase every block the area covers.
 *
 * Whole blocks, all of them first, before any of them is written.  A NAND
 * that is erased and then written block by block leaves a flash where half
 * the area is new and half is old; erased everything up front, a power cut
 * leaves a block whose MBR has not been written yet, which reads as no image
 * rather than as a corrupt one.
 */
static int erase_area(u32 first_block, u32 blocks)
{
    for (u32 b = 0; b < blocks; b++) {
        if (nand_erase_block(first_block + b)) {
            uart_printf("flashing: block 0x%x would not erase\r\n",
                        first_block + b);
            return -1;
        }
    }
    return 0;
}

/*
 * Write the OS area.
 *
 * The eight bytes of sector info behind each sector are not thrown away as
 * they once were: they go into the NAND's spare area, four bytes to a
 * codeword, which is where the phone's flash driver looks for them.  Without
 * them the area reads back perfectly and the OS still cannot find a single
 * one of its sectors, which is a confusing way to fail.
 */
static int flash_os(const struct nbh *nb, int verbose)
{
    static u8 page[NAND_PAGE_SIZE] __attribute__((aligned(4)));
    u8 spare[NAND_CODEWORDS * NAND_SPARE_LEN];
    u32 first_page = NAND_OS_BLOCK * NAND_PAGES_PER_BLK;
    u32 blocks = (nb->os_sectors + NAND_PAGES_PER_BLK - 1) / NAND_PAGES_PER_BLK;

    if (blocks > 1 && verbose) {
        uart_printf("flashing: %u sectors in %u blocks from 0x%x\r\n",
                    nb->os_sectors, blocks, (u32)NAND_OS_BLOCK);
    }
    if (erase_area(NAND_OS_BLOCK, blocks)) {
        return -1;
    }

    for (u32 s = 0; s < nb->os_sectors; s++) {
        u32 src = nb->os_off + s * NBH_STRIDE;

        if (nbh_image(nb, src, page, NBH_SECTOR) != NBH_SECTOR) {
            uart_puts("flashing: ran out of file part way through\r\n");
            return -1;
        }
        if (nbh_image(nb, src + NBH_SECTOR, spare, NBH_SECTOR_INFO) !=
            NBH_SECTOR_INFO) {
            uart_puts("flashing: the sector info is not all there\r\n");
            return -1;
        }

        /*
         * The tag lives in the spare area too, and the controller relocates
         * two of the four bytes out of the user data into the chip's own
         * bad-block marker - which is why a page's bytes 464 and 465 do not
         * come back as they went in, and why the checksums below allow for
         * it.
         */
        if (nand_write_page_ex(first_page + s, page, spare)) {
            uart_puts("flashing: a page would not program\r\n");
            return -1;
        }
        if (verbose && !(s % 256) && s) {
            uart_printf("flashing: %u/%u sectors\r\n", s, nb->os_sectors);
        }
    }

    if (verbose) {
        uart_printf("flashing: %u sectors written\r\n", nb->os_sectors);
    }
    return 0;
}

/*
 * Write every section of the file that has somewhere to go.
 *
 * The OS is placed; the rest are listed with what this does not yet know
 * about them, rather than guessed at.  A partition written to the wrong
 * block is not a small mistake - it is a radio that does not come up, and
 * there is no undo for it short of another flash.
 *
 * The block the bootloader itself is in is never written: `covers()` refuses
 * it before anything is erased, so a file that happens to describe that
 * block cannot take this one out from under itself.
 */
/*
 * Write only the OS area.
 *
 * This is what `nbhflash` does, and what the checksums are taken over.  It is
 * the part of the flash that a single-OS file - which is all cCLK's NBH ever
 * has - needs; for a stock ROM use nbh_flash_all(), which says what it did
 * not place instead of quietly doing less.
 */
int nbh_flash(const struct nbh *nb, int verbose)
{
    return flash_os(nb, verbose);
}

int nbh_flash_all(const struct nbh *nb, int verbose)
{
    u32 placed = 0;
    u32 unknown = 0;

    if (verbose) {
        uart_printf("flashing: %u sections\r\n", nb->section_count);
    }

    for (u32 i = 0; i < nb->section_count; i++) {
        const struct nbh_section *sec = &nb->section[i];

        if (sec->type == NBH_SEC_OS) {
            if (verbose) {
                uart_printf("  0x%03x OS      %u bytes -> block 0x%x\r\n",
                            sec->type, sec->len, (u32)NAND_OS_BLOCK);
            }
            if (flash_os(nb, verbose)) {
                return -1;
            }
            placed++;
            continue;
        }

        /*
         * Whatever it is, it has to know which blocks it is about to erase,
         * and the one this program is running out of is not among them.
         */
        unknown++;
        if (verbose) {
            uart_printf("  0x%03x %-8s %u bytes -> not placed: no "
                        "destination known\r\n",
                        sec->type, "", sec->len);
        }
    }

    if (verbose) {
        uart_printf("flashing: %u section(s) placed, %u not\r\n", placed,
                    unknown);
        if (unknown) {
            uart_puts("flashing: the flash is not what a full flash would "
                      "leave\r\n");
        }
    }
    return placed ? 0 : -1;
}

/*
 * The area as the flash will hold it, so that the two can be compared like for
 * like.
 *
 * Two bytes of every page are not the file's.  The part keeps its bad-block
 * marker in the last codeword's user area, and those two bytes have to read
 * 0xFF for the block to count as good - so no matter what is written there,
 * what comes back is 0xFF.  Comparing a file against a flash that has
 * dutifully protected its own health instead reports a difference that is
 * correct behaviour being read as damage.
 *
 * The file side is put through the same substitution, so the two CRCs are over
 * the same bytes and a difference means the write did not take.
 */
#define NBH_MARKER_CW       (NAND_CODEWORDS - 1)
#define NBH_MARKER_OFF      (NBH_MARKER_CW * NAND_CW_USER + BAD_BLOCK_OFFSET)

static void marker_to_ff(u8 *page)
{
    page[NBH_MARKER_OFF] = 0xff;
    page[NBH_MARKER_OFF + 1] = 0xff;
}

static u32 area_crc(const struct nbh *nb, int from_flash)
{
    static u8 page[NAND_PAGE_SIZE] __attribute__((aligned(4)));
    u32 crc = CRC_SEED;

    for (u32 s = 0; s < nb->os_sectors; s++) {
        if (from_flash) {
            if (nand_read_page(NAND_OS_BLOCK * NAND_PAGES_PER_BLK + s, page)) {
                return 0;
            }
        } else if (nbh_image(nb, nb->os_off + s * NBH_STRIDE, page,
                             NBH_SECTOR) != NBH_SECTOR) {
            return 0;
        }
        marker_to_ff(page);
        crc = crc32(page, NBH_SECTOR, crc);
    }
    return crc;
}

u32 nbh_flash_checksum(const struct nbh *nb)
{
    return area_crc(nb, 1);
}

u32 nbh_file_checksum(const struct nbh *nb)
{
    return area_crc(nb, 0);
}
