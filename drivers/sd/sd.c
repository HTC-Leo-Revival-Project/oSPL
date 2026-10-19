/*
 * sd.c - SD/MMC on SDC2, the microSD slot, in PIO.
 *
 * The register set and the command sequencing are the controller's, not this
 * driver's: an ARM MMCI register file, which is what the part has at
 * 0xA0400000.  The sequencing is the SD specification's.
 *
 * The data path is the FIFO at MCI_FIFO, polled through MCI_STATUS.  That is
 * not a compromise made for want of anything better: with translation off
 * there is no data mover to hand it to, the FIFO pulls the next bytes from the
 * card on every read, and a bootloader reading a file is not transferring
 * hundreds of megabytes.
 *
 * Derived from iBoot's own driver for this part, which is in turn from
 * HtcLeoPkg's SdCardDxe and from Linux's drivers/mmc/host/msm_sdcc.c.
 */
#include "spl.h"
#include "board.h"
#include "sd.h"

/* ---- registers -------------------------------------------------------- */

#define MCI_POWER          0x000
#define MCI_CLK            0x004
#define MCI_ARGUMENT       0x008
#define MCI_CMD            0x00c
#define MCI_RESP_CMD       0x010
#define MCI_RESP0          0x014
#define MCI_RESP1          0x018
#define MCI_RESP2          0x01c
#define MCI_RESP3          0x020
#define MCI_DATA_TIMER     0x024
#define MCI_DATA_LENGTH    0x028
#define MCI_DATA_CTL       0x02c
#define MCI_DATA_COUNT     0x030
#define MCI_STATUS         0x034
#define MCI_CLEAR          0x038
#define MCI_INT_MASK0      0x03c
#define MCI_INT_MASK1      0x040
#define MCI_FIFO_COUNT     0x044
#define MCI_FIFO           0x080

#define POWER_ON           0x3

#define CLK_DIVIDER_S      0
#define CLK_DIVIDER_M      0x0000000f
#define CLK_INT_ENA_M      0x00000020
#define CLK_ENA_M          0x00000040
#define CLK_PWRSAVE_M      0x00000200
#define CLK_WIDEBUS_M      0x00000c00
#define CLK_WIDEBUS_4BIT   0x2
#define CLK_FLOW_ENA_M     0x00001000
#define CLK_SRC_IN_S       16
#define CLK_SRC_IN_M       0x00030000

#define CMD_INDEX_M        0x0000003f
#define CMD_RESPONSE_M     0x00000040
#define CMD_LONGRSP_M      0x00000080
#define CMD_PROG_ENA_M     0x00000800
#define CMD_ENABLE_M       0x00000400

#define DATA_CTL_ENABLE_M  0x00000001
#define DATA_CTL_DIRECTION_M 0x00000002
#define DATA_CTL_BLOCKSIZE_S 4

#define ST_CMD_CRC_FAIL    0x00000001
#define ST_DATA_CRC_FAIL   0x00000002
#define ST_CMD_TIMEOUT     0x00000004
#define ST_DATA_TIMEOUT    0x00000008
#define ST_TX_UNDERRUN     0x00000010
#define ST_RX_OVERRUN      0x00000020
#define ST_CMD_RESPONSE_END 0x00000040
#define ST_CMD_SENT        0x00000080
#define ST_DATAEND         0x00000100
#define ST_DATA_BLK_END    0x00000400
#define ST_RXACTIVE        0x00002000
#define ST_TXFIFO_FULL     0x00010000
#define ST_RXDATA_AVLBL    0x00200000
#define ST_PROG_DONE       0x00800000

/*
 * The latched bits, which CLEAR takes.  Only the errors and the
 * command/data-complete bits: the rest describe live state and clearing them
 * says something untrue.
 */
#define CLR_CMDCRCFAIL     0x00000001
#define CLR_DATACRCFAIL    0x00000002
#define CLR_CMDTIMEOUT     0x00000004
#define CLR_DATATIMEOUT    0x00000008
#define CLR_TXUNDERRUN     0x00000010
#define CLR_RXOVERRUN      0x00000020
#define CLR_CMDRESPEND     0x00000040
#define CLR_CMDSENT        0x00000080
#define CLR_DATAEND        0x00000100
#define CLR_DATABLKEND     0x00000400

/*
 * CMD_RESPONSE_END is deliberately not an error.  It is the *success* flag for
 * a command that has a response, and treating it as a failure makes every
 * command that worked report that it failed - which is not subtle, because the
 * error test runs before the completion test and so CMD8 and everything after
 * it never get as far as being checked.
 */
#define ST_CMD_ERRORS      (ST_CMD_CRC_FAIL | ST_CMD_TIMEOUT)
#define ST_DATA_ERRORS     (ST_DATA_CRC_FAIL | ST_DATA_TIMEOUT | \
                           ST_RX_OVERRUN | ST_TX_UNDERRUN)

/* ---- commands --------------------------------------------------------- */

#define CMD0    0x00        /* GO_IDLE_STATE       */
#define CMD2    0x02        /* ALL_SEND_CID        */
#define CMD3    0x03        /* SET_RELATIVE_ADDR   */
#define CMD7    0x07        /* SELECT_CARD         */
#define CMD8    0x08        /* SEND_IF_COND        */
#define CMD9    0x09        /* SEND_CSD            */
#define CMD16   0x10        /* SET_BLOCKLEN        */
#define CMD17   0x11        /* READ_SINGLE_BLOCK   */
#define CMD24   0x18        /* WRITE_BLOCK         */
#define CMD55   0x37        /* APP_CMD             */
#define ACMD41  0x29        /* SD_APP_OP_COND      */

#define RSP_NONE   0
#define RSP_SHORT  4
#define RSP_LONG   16

/*
 * Bounds on the polls, in iterations.
 *
 * Both exist only so that a card which stops answering cannot spin forever,
 * and both are deliberately modest: every iteration is an MMIO access, so a
 * bound set far too high does not fail fast, it hangs - and a hang looks
 * exactly like a wedged machine.  A working command never comes near either.
 */
#define CMD_POLL_LIMIT  4096
#define FIFO_POLL_LIMIT 4096

#define DATA_TIMEOUT     (100 * 1000)

/* A card starts at 144 kHz and can only be taken faster once initialised. */
#define CLOCK_INIT_HZ     144000u
#define CLOCK_FULL_HZ     25000000u
#define CLOCK_SOURCE_HZ   144000000u

static inline u32 mci_read(u32 off)
{
    return mmio_read32(SDC2_BASE, off);
}

static inline void mci_write(u32 off, u32 val)
{
    mmio_write32(SDC2_BASE, off, val);
}

static void sd_clear_status(void)
{
    mci_write(MCI_CLEAR, CLR_CMDCRCFAIL | CLR_CMDSENT | CLR_DATAEND |
                            CLR_DATABLKEND | CLR_RXOVERRUN | CLR_TXUNDERRUN |
                            CLR_CMDTIMEOUT | CLR_CMDCRCFAIL |
                            CLR_DATATIMEOUT | CLR_DATACRCFAIL);
}

static void sd_set_clock(u32 hz)
{
    u32 divider = (CLOCK_SOURCE_HZ + hz - 1) / hz;
    u32 reg;

    /* divider 0 is /1, so 1 is /2. */
    if (divider < 1) {
        divider = 1;
    }
    if (divider > 16) {
        divider = 16;
    }

    reg = mci_read(MCI_CLK);
    reg &= ~(CLK_ENA_M | CLK_INT_ENA_M);
    mci_write(MCI_CLK, reg);

    reg &= ~(CLK_DIVIDER_M | CLK_SRC_IN_M | CLK_FLOW_ENA_M |
             CLK_WIDEBUS_M | CLK_PWRSAVE_M);
    reg |= ((divider - 1) << CLK_DIVIDER_S);
    reg |= (0x2 << CLK_SRC_IN_S);      /* PLL2, the SD clock source */
    reg |= CLK_FLOW_ENA_M;             /* clock edges are what counts */
    mci_write(MCI_CLK, reg | CLK_ENA_M | CLK_INT_ENA_M);

    /* Cards start in one-bit mode; four-bit is switched on with CMD6. */
    reg = mci_read(MCI_CLK);
    reg &= ~(CLK_WIDEBUS_M | CLK_PWRSAVE_M);
    mci_write(MCI_CLK, reg);
}

/*
 * Send one command.  For RSP_SHORT and RSP_LONG, `response` receives the words.
 *
 * Which response length a command wants is not derivable from its index, and
 * getting it wrong is not a soft failure: RESPONSE says there is a response,
 * LONGRSP says it is 136 bits, and the controller checks what the card
 * actually returned against what was asked for.  Asking for the wrong one is a
 * protocol error.
 */
static int sd_send_cmd(u32 cmd, u32 arg, u32 *response, u32 rsp_len)
{
    u32 status;
    u32 done;
    u32 spins;
    u32 cmdreg;

    sd_clear_status();
    mci_write(MCI_RESP_CMD, 0);
    mci_write(MCI_ARGUMENT, arg);

    cmdreg = (cmd & CMD_INDEX_M) | CMD_ENABLE_M;
    if (rsp_len == RSP_LONG) {
        cmdreg |= CMD_RESPONSE_M | CMD_LONGRSP_M;
    } else if (rsp_len == RSP_SHORT) {
        cmdreg |= CMD_RESPONSE_M;
    }
    mci_write(MCI_CMD, cmdreg);

    /*
     * CMD_SENT means the command went out; CMD_RESPONSE_END means the
     * response is in RESP0..3.  Either ends the wait for a command that has a
     * response bit set, and only CMD_SENT for one that does not.
     */
    done = rsp_len ? ST_CMD_RESPONSE_END : ST_CMD_SENT;

    for (spins = 0; spins < CMD_POLL_LIMIT; spins++) {
        status = mci_read(MCI_STATUS);
        if (status & ST_CMD_ERRORS) {
            return -1;
        }
        if (status & done) {
            break;
        }
    }
    if (spins == CMD_POLL_LIMIT) {
        return -1;
    }

    if (rsp_len == RSP_SHORT) {
        /* Only RESP0 carries anything for a 48-bit response; the rest are the
         * controller's own leftovers and reading them picks up stale state. */
        response[0] = mci_read(MCI_RESP0);
    } else if (rsp_len == RSP_LONG) {
        response[0] = mci_read(MCI_RESP0);
        response[1] = mci_read(MCI_RESP1);
        response[2] = mci_read(MCI_RESP2);
        response[3] = mci_read(MCI_RESP3);
    }
    return 0;
}

static void sd_go_idle(void)
{
    mci_write(MCI_CLK, 0);

    /* two off and one on, which is what a real controller wants to see */
    mci_write(MCI_POWER, POWER_ON);
    mci_write(MCI_POWER, 0);
    mci_write(MCI_POWER, POWER_ON);

    sd_set_clock(CLOCK_INIT_HZ);
}

static int sd_ident(struct sd_card *card)
{
    u32 response[4];
    u32 csd_structure;
    u32 csd6, csd7, csd8, csd9;
    u32 c_size;
    u32 arg;
    u32 spins;
    u32 retries;

    /* CMD8: we speak 2.0.  R7 echoes the pattern back in its lower 12 bits. */
    if (sd_send_cmd(CMD8, 0x1aa, response, RSP_SHORT)) {
        return -1;
    }
    if ((response[0] & 0xfff) != 0x1aa) {
        return -1;
    }

    /* ACMD41 until the card stops being busy.  CMD55 has no response of its
     * own to check. */
    for (retries = 0; retries < 1000; retries++) {
        if (sd_send_cmd(CMD55, 0, NULL, RSP_NONE)) {
            return -1;
        }
        if (sd_send_cmd(ACMD41, 0x40ff8000, response, RSP_SHORT)) {
            return -1;           /* bit 30 is HCS: block addressing */
        }
        if (response[0] & 0x80000000) {
            break;              /* out of init */
        }
        for (spins = 0; spins < 10000; spins++) {
            /* busy: a moment before asking again */
        }
    }
    if (retries == 1000) {
        return -1;
    }

    if (sd_send_cmd(CMD2, 0, response, RSP_LONG)) {
        return -1;
    }
    if (sd_send_cmd(CMD3, 0, response, RSP_SHORT)) {
        return -1;
    }
    card->rca = response[0] >> 16;

    arg = card->rca << 16;
    if (sd_send_cmd(CMD9, arg, response, RSP_LONG)) {
        return -1;
    }
    for (int i = 0; i < 4; i++) {
        card->csd[i] = response[i];
    }

    /* Select the card, so later commands have somewhere to go. */
    if (sd_send_cmd(CMD7, card->rca << 16, NULL, RSP_NONE)) {
        return -1;
    }

    /*
     * CSD_STRUCTURE is bits 127:126, which are the top two bits of the CSD's
     * *first* byte - word 0, not the last one.  0 is v1.0 and byte-addressed;
     * 1 is v2.0 and block-addressed.
     *
     * The CSD arrives as four big-endian words, byte k in word k/4 at bit
     * (3 - k%4)*8.  That is the register layout; the spec defines the response
     * MSB first, so the first word read is the first bytes of the CSD.
     *
     * Both versions encode capacity as (C_SIZE + 1) * 512 KiB.  What differs
     * is where C_SIZE lives and whether CMD17 takes a block or a byte offset.
     * Getting that bit field wrong is not a soft failure: it gives a
     * plausible number out by a factor of a million, so the two are written
     * out separately rather than shared.
     */
    csd_structure = (card->csd[0] >> 30) & 3;
    csd6 = (card->csd[1] >> 8) & 0xff;
    csd7 = card->csd[1] & 0xff;
    csd8 = (card->csd[2] >> 24) & 0xff;
    csd9 = (card->csd[2] >> 16) & 0xff;

    if (csd_structure == 0) {
        card->byte_addressed = 1;
        c_size = (((csd6 >> 1) & 1) << 10) | ((csd7 & 0xff) << 2) |
                 ((csd8 >> 6) & 3);
    } else {
        card->byte_addressed = 0;
        c_size = (((csd7 >> 5) & 1) << 16) | ((csd8 & 0xff) << 8) |
                 (csd9 & 0xff);
    }
    card->blocks = ((u64)c_size + 1) * 1024;

    if (sd_send_cmd(CMD16, SD_BLOCK_SIZE, NULL, RSP_NONE)) {
        return -1;
    }

    /* out of init state, so it can go to full speed */
    sd_set_clock(CLOCK_FULL_HZ);
    card->present = 1;
    return 0;
}

/*
 * The card is identified once and then kept.
 *
 * Going back to idle is a power cycle - the controller wants the two off, one
 * on sequence - and a card that has just been identified does not always come
 * back through it in the time a menu command can afford to wait.  More to the
 * point there is no reason to: nothing this does resets the card, and the
 * alternative is a filesystem that has to re-identify before every read.
 */
static struct sd_card identified;

int sd_init(struct sd_card *card)
{
    if (!identified.present) {
        memset(&identified, 0, sizeof(identified));
        sd_go_idle();
        if (sd_ident(&identified)) {
            return -1;
        }
    }
    *card = identified;
    return 0;
}

static u32 sd_block_address(const struct sd_card *card, u64 block)
{
    return card->byte_addressed ? (u32)(block * SD_BLOCK_SIZE) : (u32)block;
}

int sd_read_block(const struct sd_card *card, u64 block, void *dst)
{
    u8 *p = dst;
    u8 *end = p + SD_BLOCK_SIZE;
    u32 status;
    u32 spins;

    mci_write(MCI_DATA_TIMER, DATA_TIMEOUT);
    mci_write(MCI_DATA_LENGTH, SD_BLOCK_SIZE);

    /*
     * ENABLE starts the data path and DIRECTION says read.  DM_ENABLE stays
     * clear: that is the data mover, which needs a linked list in RAM and
     * completion callbacks, none of which a bootloader has.
     */
    mci_write(MCI_DATA_CTL, DATA_CTL_ENABLE_M | DATA_CTL_DIRECTION_M |
                               (SD_BLOCK_SIZE << DATA_CTL_BLOCKSIZE_S));

    sd_clear_status();
    mci_write(MCI_ARGUMENT, sd_block_address(card, block));
    mci_write(MCI_CMD, CMD17 | CMD_ENABLE_M | CMD_RESPONSE_M);

    /* the response comes before the data, and an error means no data */
    for (spins = 0; spins < CMD_POLL_LIMIT; spins++) {
        status = mci_read(MCI_STATUS);
        if (status & (ST_CMD_ERRORS | ST_DATA_ERRORS)) {
            return -1;
        }
        if (status & (ST_CMD_RESPONSE_END | ST_RXDATA_AVLBL)) {
            break;
        }
    }
    if (spins == CMD_POLL_LIMIT) {
        return -1;
    }

    /* Drain the FIFO: every read pulls the next bytes from the card. */
    for (spins = 0; spins < FIFO_POLL_LIMIT && p < end; spins++) {
        status = mci_read(MCI_STATUS);
        if (status & ST_RXDATA_AVLBL) {
            *(volatile u32 *)p = mci_read(MCI_FIFO);
            p += 4;
        } else if (!(status & (ST_RXACTIVE | ST_RXDATA_AVLBL))) {
            break;              /* neither data nor an active read: done */
        }
    }
    if (p != end) {
        return -1;
    }

    /* let the controller latch the end-of-block so STATUS reads true */
    for (spins = 0; spins < FIFO_POLL_LIMIT; spins++) {
        if (mci_read(MCI_STATUS) & ST_DATA_BLK_END) {
            break;
        }
    }
    return 0;
}

int sd_write_block(const struct sd_card *card, u64 block, const void *src)
{
    const u8 *p = src;
    const u8 *end = p + SD_BLOCK_SIZE;
    u32 status;
    u32 spins;

    mci_write(MCI_DATA_TIMER, DATA_TIMEOUT);
    mci_write(MCI_DATA_LENGTH, SD_BLOCK_SIZE);
    mci_write(MCI_DATA_CTL, DATA_CTL_ENABLE_M |
                               (SD_BLOCK_SIZE << DATA_CTL_BLOCKSIZE_S));

    sd_clear_status();
    mci_write(MCI_ARGUMENT, sd_block_address(card, block));
    mci_write(MCI_CMD, CMD24 | CMD_ENABLE_M | CMD_RESPONSE_M);

    for (spins = 0; spins < CMD_POLL_LIMIT; spins++) {
        status = mci_read(MCI_STATUS);
        if (status & (ST_CMD_ERRORS | ST_DATA_ERRORS)) {
            return -1;
        }
        if (status & ST_CMD_RESPONSE_END) {
            break;
        }
    }
    if (spins == CMD_POLL_LIMIT) {
        return -1;
    }

    while (p < end) {
        mci_write(MCI_FIFO, read_le32(p));
        p += 4;
        for (spins = 0; spins < FIFO_POLL_LIMIT; spins++) {
            status = mci_read(MCI_STATUS);
            if (status & ST_DATA_ERRORS) {
                return -1;
            }
            if (!(status & (ST_TXFIFO_FULL | ST_TX_UNDERRUN))) {
                break;          /* room for the next word */
            }
        }
    }

    mci_write(MCI_CMD, CMD24 | CMD_PROG_ENA_M | CMD_ENABLE_M);
    for (spins = 0; spins < FIFO_POLL_LIMIT; spins++) {
        status = mci_read(MCI_STATUS);
        if (status & ST_DATA_ERRORS) {
            return -1;
        }
        if (status & (ST_DATAEND | ST_PROG_DONE)) {
            break;
        }
    }
    return (spins == FIFO_POLL_LIMIT) ? -1 : 0;
}

int sd_read_blocks(const struct sd_card *card, u64 first, u32 count, void *dst)
{
    u8 *out = dst;

    for (u32 i = 0; i < count; i++) {
        if (sd_read_block(card, first + i, out + (unsigned long)i * SD_BLOCK_SIZE)) {
            return -1;
        }
    }
    return 0;
}
