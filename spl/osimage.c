/*
 * osimage.c - the "OS" area of the NAND.
 *
 * The area starts at block 0x212, one 2048-byte sector per page, and is
 * laid out the way HTC's own flashing tools leave it:
 *
 *   sector 0        a WinCE MBR: E9 FD FF ... 55 AA, with one partition
 *                   entry of type 0x23 at 0x1BE whose StartSector and
 *                   TotalSectors say where the image is
 *   sector 1        the "MSFLSH50" tag
 *   sector 2 ..     the image
 *
 * The image is loaded at OS_IMAGE_BASE and entered there with the MMU off.
 * Nothing here needs to know what the image is.
 */
#include "spl.h"
#include "board.h"
#include "nand.h"
#include "osimage.h"
#include "drivers/uart/uart.h"

/*
 * One 16-byte MBR partition entry, struct PartEntry.
 *
 * The table starts at 0x1BE, so StartSector lands at 0x1C6 and TotalSectors
 * at 0x1CA - two bytes into a word, not four.  packed records that, and
 * read_le32() below is what the two 32-bit fields have to go through: left
 * to itself the compiler would widen the four byte reads back into one
 * aligned LDR, and that address is not aligned.
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
#define PART_TYPE          0x23    /* Windows Mobile                 */
#define MBR_SIG_55         0x55
#define MBR_SIG_AA         0xaa

/* the tag on the sector in front of the image */
#define IMAGE_TAG_SECTOR   1
#define IMAGE_TAG          "MSFLSH50"

static u8 sector[NAND_PAGE_SIZE] __attribute__((aligned(4)));

int osimage_locate(struct os_image *img)
{
    const struct mbr_part *part;
    u32 start, total;
    u32 i;

    img->base = OS_IMAGE_BASE;
    img->size = 0;
    img->start = 0;
    img->sectors = 0;

    if (nand_read_page(NAND_OS_BLOCK * NAND_PAGES_PER_BLK, sector)) {
        uart_puts("OS area: the flash did not read\r\n");
        return -1;
    }

    /* the MBR's own signature, then the 55 AA at the end */
    if (sector[0] != MBR_SIG0 || sector[1] != MBR_SIG1 ||
        sector[2] != MBR_SIG2 ||
        sector[510] != MBR_SIG_55 || sector[511] != MBR_SIG_AA) {
        uart_puts("OS area: no MBR here\r\n");
        return -2;
    }

    part = (const struct mbr_part *)(const void *)(sector + MBR_PART_OFF);
    if (part->type != PART_TYPE) {
        uart_puts("OS area: no Windows Mobile partition\r\n");
        return -3;
    }

    start = read_le32(&part->start_sector);
    total = read_le32(&part->total_sectors);

    /* the image has to be inside the area, and has to fit in the RAM we clear */
    if (total == 0 || start < IMAGE_TAG_SECTOR + 1) {
        uart_puts("OS area: the partition is empty\r\n");
        return -4;
    }
    if ((u64)total * NAND_PAGE_SIZE > OS_IMAGE_MAX) {
        uart_puts("OS area: the image does not fit in RAM\r\n");
        return -5;
    }

    /* the sector in front of the image carries the tag */
    if (nand_read_page(NAND_OS_BLOCK * NAND_PAGES_PER_BLK +
                       IMAGE_TAG_SECTOR, sector)) {
        uart_puts("OS area: the flash did not read\r\n");
        return -6;
    }
    for (i = 0; i < sizeof(IMAGE_TAG) - 1; i++) {
        if (sector[i] != IMAGE_TAG[i]) {
            uart_puts("OS area: no " IMAGE_TAG " tag\r\n");
            return -7;
        }
    }

    img->start = start;
    img->sectors = total;
    img->size = total * NAND_PAGE_SIZE;
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