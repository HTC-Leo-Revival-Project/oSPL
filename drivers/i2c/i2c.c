/*
 * i2c.c - the MSM's legacy I2C controller at 0xa9900000.
 *
 * One register holds everything.  A write with bit 8 set starts a transfer:
 * the low seven bits are the address shifted left with the direction in bit
 * zero.  After that a plain write sends a byte, and a read fetches one - the
 * controller has already gone and got the next one ready, so a read is a
 * pop rather than a wait.
 *
 * Writing LAST_BYTE while reading is how the last byte is called: it says the
 * byte that has just been fetched is the one before the end, so the fetch
 * after it ends the transfer rather than starting another.  Which is why
 * there is a read loop with a condition in it rather than a simple one.
 */
#include "spl.h"
#include "board.h"
#include "i2c.h"

#define I2C_BASE          0xA9900000u
#define I2C_WRITE_DATA    0x00
#define I2C_STATUS        0x08
#define I2C_READ_DATA     0x0c

/* a write with this bit set starts a transfer rather than sending a byte */
#define WD_ADDR_BYTE      (1u << 8)
#define WD_LAST_BYTE      (1u << 9)

#define ST_NACKED         (1u << 3)
#define ST_FAILED         (3u << 6)

static void start(u32 slave, int reading)
{
    mmio_write32(I2C_BASE, I2C_WRITE_DATA,
                 WD_ADDR_BYTE | (slave << 1) | (reading ? 1u : 0u));
}

static int failed(void)
{
    u32 st = mmio_read32(I2C_BASE, I2C_STATUS);

    /* reading the status is also what clears the error for next time */
    return (st & (ST_NACKED | ST_FAILED)) ? -1 : 0;
}

int i2c_write(u32 slave, u32 reg, const void *buf, u32 n)
{
    const u8 *out = (const u8 *)buf;

    start(slave, 0);
    if (failed()) {
        return -1;
    }
    mmio_write32(I2C_BASE, I2C_WRITE_DATA, (reg << 8) >> 8);
    for (u32 i = 0; i < n; i++) {
        u32 v = out[i];

        if (i + 1 == n) {
            v |= WD_LAST_BYTE;
        }
        mmio_write32(I2C_BASE, I2C_WRITE_DATA, v);
    }
    return failed();
}

int i2c_read(u32 slave, u32 reg, void *buf, u32 n)
{
    u8 *in = (u8 *)buf;

    if (!n) {
        return 0;
    }
    /*
     * Point the device at a register first.  It is a transfer of its own,
     * which is what makes this a repeated start on real hardware.
     */
    if (reg <= 0xff) {
        if (i2c_write(slave, reg, NULL, 0) < 0) {
            return -1;
        }
    }

    start(slave, 1);
    if (failed()) {
        return -1;
    }
    for (u32 i = 0; i < n; i++) {
        if (n - i == 2) {
            mmio_write32(I2C_BASE, I2C_WRITE_DATA, WD_LAST_BYTE);
        }
        in[i] = (u8)mmio_read32(I2C_BASE, I2C_READ_DATA);
    }
    return failed();
}