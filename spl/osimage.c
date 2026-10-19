/*
 * osimage.c - the "OS" area of the NAND.
 *
 * The area starts at block 0x212, one 2048-byte sector per page, and is
 * laid out the way HTC's own flashing tools leave it:
 *
 *   sector 0        a WinCE MBR: E9 FD ... 55 AA, with four partition
 *                   entries 16 bytes apart at 0x1BE, whose StartSector
 *                   and TotalSectors say where each thing is
 *   sector 1        the "MSFLSH50" tag
 *   sector 2 ..     the image
 *
 * The image is loaded at OS_IMAGE_BASE and entered there with the MMU off.
 * Nothing here needs to know what the image is.
 *
 * The partition table is read the way the phone's own bootloader reads it,
 * which was worth checking against: it walks four entries 16 bytes apart and
 * takes the StartSector at +0x1C6 and the TotalSectors at +0x1CA out of each
 * *a byte at a time*.  Those offsets are two bytes into a word, and with
 * translation off the default memory type is Device, which does not do
 * unaligned transfers - so a word load there is a fault rather than a read.
 * One of them is what this used to do, and it cost an afternoon.
 */
#include "spl.h"
#include "board.h"
#include "nand.h"
#include "osimage.h"
#include "drivers/uart/uart.h"

/*
 * One 16-byte MBR partition entry, struct PartEntry.  The fields are read
 * through read_le32() rather than through the struct, because the compiler is
 * entitled to widen four byte reads back into one aligned word load and that
 * address is not aligned.
 */
struct mbr_part {
    u8  boot;          /* 0x00: 0x80 if the partition is active      */
    u8  head[3];       /* 0x01: CHS of the first sector             */
    u8  type;          /* 0x04: FileSystem                           */
    u8  tail[3];       /* 0x05: CHS of the last sector              */
    u32 start_sector;  /* 0x08: LBA of the first sector              */
    u32 total_sectors; /* 0x0c: how many sectors it holds            */
} __attribute__((packed));

#define MBR_SIG0           0xe9
#define MBR_SIG1           0xfd
#define MBR_SIG2           0xff
#define MBR_PART_OFF       0x1be
#define MBR_ENTRY_LEN      16      /* one partition entry              */
#define MBR_PARTS          4       /* and there are this many of them */
#define PART_TYPE          0x23    /* Windows Mobile                 */
#define MBR_SIG_55         0x55
#define MBR_SIG_AA         0xaa

/* the tag on the sector in front of the image */
#define IMAGE_TAG_SECTOR   1
#define IMAGE_TAG          "MSFLSH50"

static u8 sector[NAND_PAGE_SIZE] __attribute__((aligned(4)));

static const struct mbr_part *mbr_entry(int i)
{
    return (const struct mbr_part *)(const void *)
           (sector + MBR_PART_OFF + (unsigned long)i * MBR_ENTRY_LEN);
}

int osimage_locate(struct os_image *img)
{
    int found = -1;

    img->base = OS_IMAGE_BASE;
    img->size = 0;
    img->start = 0;
    img->sectors = 0;
    img->part = 0;

    if (nand_read_page(NAND_OS_BLOCK * NAND_PAGES_PER_BLK, sector)) {
        uart_puts("OS area: the flash did not read\n");
        return -1;
    }

    /* the MBR's own signature, then the 55 AA at the end */
    if (sector[0] != MBR_SIG0 || sector[1] != MBR_SIG1 ||
        sector[2] != MBR_SIG2 ||
        sector[510] != MBR_SIG_55 || sector[511] != MBR_SIG_AA) {
        uart_puts("OS area: no MBR here\n");
        return -2;
    }

    /*
     * Walk the table rather than taking entry zero: the phone's bootloader
     * looks at all four, and an image flashed into a later partition is
     * perfectly normal.
     */
    for (int i = 0; i < MBR_PARTS; i++) {
        const struct mbr_part *p = mbr_entry(i);

        if (p->type == PART_TYPE &&
            read_le32(&p->total_sectors)) {
            found = i;
            break;
        }
    }
    if (found < 0) {
        uart_puts("OS area: no Windows Mobile partition\n");
        return -3;
    }

    img->part = (u32)found;
    img->start = read_le32(&mbr_entry(found)->start_sector);
    img->sectors = read_le32(&mbr_entry(found)->total_sectors);

    /* the image has to sit inside the area, and fit in the space we clear */
    if (img->sectors == 0 || img->start < IMAGE_TAG_SECTOR + 1) {
        uart_puts("OS area: the partition is empty\n");
        return -4;
    }
    if ((u64)img->sectors * NAND_PAGE_SIZE > OS_IMAGE_MAX) {
        uart_puts("OS area: the image does not fit in RAM\n");
        return -5;
    }
    img->size = img->sectors * NAND_PAGE_SIZE;

    /* the sector in front of the image carries the tag */
    if (nand_read_page(NAND_OS_BLOCK * NAND_PAGES_PER_BLK +
                       IMAGE_TAG_SECTOR, sector)) {
        uart_puts("OS area: the flash did not read\n");
        return -6;
    }
    for (u32 i = 0; i < sizeof(IMAGE_TAG) - 1; i++) {
        if (sector[i] != IMAGE_TAG[i]) {
            uart_puts("OS area: no " IMAGE_TAG " tag\n");
            return -7;
        }
    }
    return 0;
}

int osimage_load(const struct os_image *img)
{
    /*
     * Straight out of the flash into the load address.  nand_read_run()
     * starts at the sector the MBR named and steps over bad blocks on the
     * way, so the image lands contiguously however the part is laid out.
     */
    u32 first_page = NAND_OS_BLOCK * NAND_PAGES_PER_BLK + img->start;

    return nand_read_run(first_page, img->sectors, (void *)(u32)img->base);
}

/*
 * Write the MBR of an image area.
 *
 * This is the other half of flashing: everything above finds an image that
 * was written here, and this is how it gets there.  Only the one partition is
 * described - the entry has to be there before the image that goes with it,
 * so that a power cut leaves an area that is simply empty rather than one
 * that claims to hold something it does not.
 */
int osimage_write_mbr(u32 part, u32 start_sector, u32 sectors)
{
    struct mbr_part *p;
    u8 *mbr;

    if (part >= MBR_PARTS) {
        return -1;
    }

    if (nand_read_page(NAND_OS_BLOCK * NAND_PAGES_PER_BLK, sector)) {
        return -1;
    }
    mbr = sector;

    /* the signature, so that a half-written area is never taken for a real
     * one: erase the page and write all of it in one go */
    memset(mbr, 0, NAND_PAGE_SIZE);
    mbr[0] = MBR_SIG0;
    mbr[1] = MBR_SIG1;
    mbr[2] = MBR_SIG2;
    mbr[510] = MBR_SIG_55;
    mbr[511] = MBR_SIG_AA;

    p = (struct mbr_part *)(void *)(mbr + MBR_PART_OFF +
                                    part * MBR_ENTRY_LEN);
    p->boot = 0x80;
    p->type = PART_TYPE;
    p->start_sector = start_sector;
    p->total_sectors = sectors;

    /*
     * Programming a NAND only clears bits, so the page has to be erased
     * first.  Sector 0 is in the block the MBR lives in.
     */
    if (nand_erase_block(NAND_OS_BLOCK)) {
        return -1;
    }
    return nand_write_page(NAND_OS_BLOCK * NAND_PAGES_PER_BLK, mbr);
}

/* The tag on the sector in front of an image. */
int osimage_write_tag(u32 start_sector)
{
    memset(sector, 0xff, NAND_PAGE_SIZE);
    memcpy(sector, IMAGE_TAG, sizeof(IMAGE_TAG) - 1);
    if (nand_erase_block((NAND_OS_BLOCK * NAND_PAGES_PER_BLK +
                          start_sector - 1) / NAND_PAGES_PER_BLK)) {
        return -1;
    }
    return nand_write_page(NAND_OS_BLOCK * NAND_PAGES_PER_BLK +
                           start_sector - 1, sector);
}