/*
 * nand.c - the MSM NAND controller's flash interface.
 *
 * The controller sits behind the data mover's port on real hardware: a driver
 * writes FLASH_CMD/ADDR0/ADDR1/CHIP_SELECT and DEV0_CFG0/1, kicks EXEC_CMD
 * once per 528-byte codeword and the codeword lands in the FLASH_BUFFER
 * window.  Registers are drivers/mtd/devices/msm_nand.h.
 *
 * A page is four codewords of 528 bytes: 512 bytes of user data, 10 bytes of
 * ECC, 4 bytes of spare and 2 more bytes on this x16 part.  The part marks a
 * block bad by putting a value other than 0xFF in the *user* area at byte 465
 * of the last codeword, which displaces two user bytes; in ECC mode the
 * controller puts those two back where they belong and hands the marker over
 * in BUFFER_STATUS[31:16].  Asking for that (CFG1 bit 16 clear) is what makes
 * every 512-byte half of the buffer plain user data.
 */
#include "spl.h"
#include "board.h"
#include "nand.h"

/* registers */
#define FLASH_CMD          0x00
#define ADDR0              0x04
#define ADDR1              0x08
#define FLASH_CHIP_SELECT  0x0c
#define EXEC_CMD           0x10
#define FLASH_STATUS       0x14
#define BUFFER_STATUS      0x18
#define DEV0_CFG0          0x20
#define DEV0_CFG1          0x24
#define READ_ID            0x40
#define READ_STATUS        0x44
#define FLASH_BUFFER       0x100
#define BUFFER_SIZE        0x400

/* commands */
#define CMD_SOFT_RESET     0x01
#define CMD_FETCH_ID       0x0b
#define CMD_STATUS         0x0c
#define CMD_RESET          0x0d
#define CMD_PAGE_READ      0x32
#define CMD_PAGE_READ_ECC  0x33
#define CMD_PAGE_READ_ALL  0x34
#define CMD_BLOCK_ERASE    0x3a

/* READ_STATUS bits */
#define STATUS_READY       0x20

/*
 * DEV0_CFG1:
 *   bit 0    1 = raw (ECC off), 0 = ECC on.  We want ECC on.
 *   bit 1    x16 part, so the bad-block marker is two bytes wide.
 *   bits 9:6 BAD_BLOCK_BYTE_NUM + 1, counting from the first byte of the
 *           codeword.  This part keeps the factory marker at 464, which is
 *           where HTC's own tooling puts it, and CFG1 comes out at 0x7442.
 *   bit 16   1 = the marker stays in the user area, 0 = relocate it.  We
 *           want it relocated, so the buffer's user area is already clean.
 */
#define CFG1_ECC_ON        0x0000
#define CFG1_X16           0x0002
#define CFG1_BB_POS(n)     (((n) + 1u) << 6)
#define BAD_BLOCK_OFFSET   464u
#define CFG1_READ          (CFG1_ECC_ON | CFG1_X16 | \
                            CFG1_BB_POS(BAD_BLOCK_OFFSET))

/*
 * DEV0_CFG0 bits 8:6 say how many codewords one EXEC_CMD sequence covers;
 * a whole page is four, and the driver kicks EXEC_CMD once for each.
 */
#define CFG0_NCODEWORDS(n) (((n) - 1u) << 6)
#define CFG0_PAGE          CFG0_NCODEWORDS(NAND_CODEWORDS)

/* BUFFER_STATUS[31:16] is the bad-block marker the controller found */
#define BB_STATUS(b)       (((b) >> 16) & 0xffff)

/* one page of scratch, for looking at a block's first page and walking off */
static u8 page_scratch[NAND_PAGE_SIZE] __attribute__((aligned(4)));

static inline void nand_cmd(u8 c)
{
    mmio_write32(NAND_BASE, FLASH_CMD, c);
}

static inline void nand_exec(void)
{
    mmio_write32(NAND_BASE, EXEC_CMD, 1);
}

static inline void nand_wait_ready(void)
{
    /* writing these registers clears them, so poll for the ready bit */
    while ((mmio_read32(NAND_BASE, READ_STATUS) & STATUS_READY) == 0) {
    }
}

static inline void nand_chip_select(int on)
{
    /*
     * The chip select is active low and lives on a GPIO1 output; drive it by
     * value through the pin's shadow so we do not depend on the direction
     * register the last owner left behind.
     */
    *(volatile u32 *)(GPIO1_BASE + 0x04) = on ? (1u << GPIO_NAND_CS) : 0u;
}

int nand_init(struct nand_info *info)
{
    u32 id;

    /* soft reset first, then the real one, and wait for the part to settle */
    nand_cmd(CMD_SOFT_RESET);
    nand_exec();
    nand_chip_select(1);

    mmio_write32(NAND_BASE, DEV0_CFG0, CFG0_PAGE);
    mmio_write32(NAND_BASE, DEV0_CFG1, CFG1_READ);

    nand_cmd(CMD_RESET);
    nand_exec();
    nand_wait_ready();

    nand_cmd(CMD_FETCH_ID);
    nand_exec();
    id = mmio_read32(NAND_BASE, READ_ID);

    info->id.maker = (u8)(id & 0xff);
    info->id.device = (u8)((id >> 8) & 0xff);
    info->pages_per_block = NAND_PAGES_PER_BLK;
    info->num_blocks = NAND_NUM_BLOCKS;

    /*
     * The maker and device ids come off the part in the controller's order;
     * a part we do not know is not one we can safely drive.
     */
    if (info->id.maker != 0xec && info->id.maker != 0x2c) {
        return -1;
    }
    return 0;
}

/*
 * Read one page.  The controller latches the address on the first EXEC_CMD of
 * a sequence and then walks the codewords itself, so the page address is set
 * once and EXEC_CMD is kicked four times.
 */
int nand_read_page(u32 page, void *dst)
{
    u8 *out = (u8 *)dst;

    if (page >= NAND_NUM_BLOCKS * NAND_PAGES_PER_BLK) {
        return -1;
    }

    mmio_write32(NAND_BASE, DEV0_CFG0, CFG0_PAGE);
    mmio_write32(NAND_BASE, DEV0_CFG1, CFG1_READ);
    /*
     * The address is split: ADDR0 carries the column in bits 15:0 and page
     * bits 15:0 in bits 31:16, ADDR1 page bits 23:16.  The page number is
     * 18 bits wide on a 4096-block part, so it has to be split rather than
     * shifted up in one go or the top 16 bits fall off the end.
     */
    mmio_write32(NAND_BASE, ADDR0, (page & 0xffffu) << 16);  /* column 0 */
    mmio_write32(NAND_BASE, ADDR1, (page >> 16) & 0xffu);
    nand_cmd(CMD_PAGE_READ_ECC);

    for (u32 cw = 0; cw < NAND_CODEWORDS; cw++) {
        nand_exec();
        io_barrier();
        /*
         * 512 of the 528 bytes: with CFG1 above the controller has already
         * taken the bad-block marker out of the user area and left the ECC,
         * the spare area and the marker's own bytes behind it, so the front
         * of the buffer is exactly the page's user data.
         */
        for (u32 i = 0; i < NAND_CW_USER; i += 4) {
            writel(out + cw * NAND_CW_USER + i,
                  mmio_read32(NAND_BASE, FLASH_BUFFER + i));
        }
    }
    return 0;
}

int nand_block_is_bad(u32 block)
{
    u32 bstatus;

    if (block >= NAND_NUM_BLOCKS) {
        return 0;
    }
    (void)nand_read_page(block * NAND_PAGES_PER_BLK, page_scratch);
    bstatus = mmio_read32(NAND_BASE, BUFFER_STATUS);

    /*
     * A good block's marker reads 0xFFFF.  Anything else - including a page
     * of all 0xFF, where the marker's own byte reads 0xFF - means the
     * factory marked it, and only a value other than 0xFF counts.
     */
    return BB_STATUS(bstatus) != 0xffff;
}

int nand_read_pages(u32 page, void *dst, u32 count)
{
    u8 *out = (u8 *)dst;

    for (u32 i = 0; i < count; i++) {
        int rc = nand_read_page(page + i, out + (unsigned long)i *
                                            NAND_PAGE_SIZE);

        if (rc) {
            return rc;
        }
    }
    return 0;
}

int nand_read_run(u32 first_page, u32 npages, void *dst)
{
    u8 *out = (u8 *)dst;
    u32 page = first_page;
    u32 total_pages = NAND_NUM_BLOCKS * NAND_PAGES_PER_BLK;

    while (npages) {
        u32 blk = page / NAND_PAGES_PER_BLK;
        u32 chunk;

        if (blk >= NAND_NUM_BLOCKS) {
            return -1;
        }
        if (nand_block_is_bad(blk)) {
            /* step over the whole block, the part is unusable there */
            page = (blk + 1) * NAND_PAGES_PER_BLK;
            continue;
        }

        /* take what is left of this block, and no more */
        chunk = (blk + 1) * NAND_PAGES_PER_BLK - page;
        if (chunk > npages) {
            chunk = npages;
        }
        if (chunk > total_pages - page) {
            chunk = total_pages - page;
        }
        if (nand_read_pages(page, out, chunk)) {
            return -1;
        }
        out += (unsigned long)chunk * NAND_PAGE_SIZE;
        page += chunk;
        npages -= chunk;
    }
    return 0;
}
