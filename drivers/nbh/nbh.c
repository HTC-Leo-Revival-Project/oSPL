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
#define NBH_AREALEN_OFF    0x140   /* bytes of OS area that follow     */
#define NBH_VERSION_OFF    0x1e0
#define NBH_VERSION_LEN    16
#define NBH_BUILD_OFF      0x1f0
#define NBH_BUILD_LEN      8

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

/* Read one field out of the joined image, a word at a time. */
static u32 image_le32(const struct nbh *nb, u32 off, u32 *ok)
{
    u8 b[4];

    if (nbh_image(nb, off, b, 4) != 4) {
        *ok = 0;
        return 0;
    }
    return (u32)b[0] | ((u32)b[1] << 8) | ((u32)b[2] << 16) |
           ((u32)b[3] << 24);
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
    u32 ok = 1;
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
    if (memcmp(f + NBH_SIG_OFF, NBH_SIG, NBH_SIG_LEN)) {
        *why = "the magic is right but the signature is not";
        return NBH_E_SIGNATURE;
    }

    memset(nb, 0, sizeof(*nb));
    nb->base = addr;
    nb->file_len = avail;

    /* the block list: every block's data, and where it lands when joined */
    while (off + NBH_BLOCK_HDR <= avail && nb->block_count < NBH_MAX_BLOCKS) {
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

        nb->block[nb->block_count].off = data;
        nb->block[nb->block_count].len = len;
        nb->block[nb->block_count].image_off = image_len;
        nb->block_count++;
        image_len += len;
        off = data + len + siglen;
    }

    if (!nb->block_count) {
        *why = "no blocks in it";
        return NBH_E_MAGIC;
    }
    nb->image_len = image_len;

    /*
     * The area's length is in the header, but the MBR at 0x200 is what
     * actually has to be right: it is the thing the flash is read back
     * through, so a file whose header and MBR disagree has to be refused
     * rather than half believed.  The header's number is used as the
     * expected length.
     */
    nb->os_off = NBH_OS_OFF;
    nb->os_bytes = image_le32(nb, NBH_AREALEN_OFF, &ok);
    if (!ok) {
        *why = "the area length is past the end of what arrived";
        return NBH_E_TRUNCATED;
    }
    if (nb->os_bytes < NBH_STRIDE) {
        *why = "the area it describes is smaller than one sector";
        return NBH_E_MAGIC;
    }
    nb->os_sectors = nb->os_bytes / NBH_STRIDE;
    nb->os_flash_bytes = nb->os_sectors * NBH_SECTOR;

    /* the MBR, and the partition inside it that names the image */
    if (nbh_image(nb, NBH_OS_OFF, buf, 512) != 512) {
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
    /* and the area has to fit in the block it is written to */
    if (nb->os_flash_bytes > NAND_BLOCK_SIZE) {
        *why = "the area is larger than the block it is written to";
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
int nbh_flash(const struct nbh *nb, int verbose)
{
    static u8 page[NAND_PAGE_SIZE] __attribute__((aligned(4)));
    u32 first_page = NAND_OS_BLOCK * NAND_PAGES_PER_BLK;

    if (verbose) {
        uart_puts("flashing: erasing the block\r\n");
    }
    if (nand_erase_block(NAND_OS_BLOCK)) {
        uart_puts("flashing: the block would not erase\r\n");
        return -1;
    }

    for (u32 s = 0; s < nb->os_sectors; s++) {
        u32 src = nb->os_off + s * NBH_STRIDE;

        /*
         * Only the 2048 bytes of the sector go in the flash; the eight bytes
         * of sector info after it are what the file needed and the flash
         * never had.
         */
        if (nbh_image(nb, src, page, NBH_SECTOR) != NBH_SECTOR) {
            uart_puts("flashing: ran out of file part way through\r\n");
            return -1;
        }
        if (nand_write_page(first_page + s, page)) {
            uart_puts("flashing: a page would not program\r\n");
            return -1;
        }
        if (verbose && ((s & 7) == 7 || s + 1 == nb->os_sectors)) {
            uart_printf("flashing: %u/%u sectors\r\n", s + 1,
                        nb->os_sectors);
        }
    }

    if (verbose) {
        uart_puts("flashing: written\r\n");
    }
    return 0;
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
