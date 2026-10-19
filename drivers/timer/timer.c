/*
 * timer.c - delays, off the CSR's timers.
 *
 * The CSR block at 0xAC100000 has two of them, each with the same four
 * registers:
 *
 *   GPT      +0x00   32768 Hz, counts whether or not it is enabled
 *   DGT      +0x10   19.2 MHz / (DGT_DIV + 1), counts only while enabled
 *   DGT_DIV  +0x20   the divider, 3 being /4, so 4.8 MHz
 *
 * and within each, MATCH +0x0, COUNT +0x4, ENABLE +0x8, CLEAR +0xC.  A write
 * to CLEAR zeroes the count, and the read right after it reads back that
 * zero - which is how you get somewhere to measure from.
 *
 * The arithmetic below stays in 32 bits on purpose.  Scaling microseconds or
 * milliseconds into ticks the long way round wants a 64-bit divide, and the
 * only implementation of that on this target is a library call we have no
 * business dragging into a bootloader.
 */
#include "spl.h"
#include "board.h"
#include "timer.h"

#define GPT_BASE     0x00
#define DGT_BASE     0x10
#define TMR_MATCH    0x00
#define TMR_COUNT    0x04
#define TMR_ENABLE   0x08
#define TMR_CLEAR    0x0c
#define DGT_DIV      0x20

/* /4, which is where the rest of the machine leaves it */
#define DGT_DIVIDER  3u
#define DGT_HZ       (19200000u / (DGT_DIVIDER + 1u))   /* 4.8 MHz */

/* Whole ticks per unit at that rate, worked out to stay in 32 bits:
 * a millisecond is exactly 4800, and a microsecond is 48/10. */
#define TICKS_PER_MS 4800u
#define TICKS_PER_US_NUM 48u
#define TICKS_PER_US_DEN 10u

/* Start the DGT and return the count it is running from. */
static u32 dgt_start(void)
{
    mmio_write32(CSR_BASE, DGT_DIV, DGT_DIVIDER);
    mmio_write32(CSR_BASE, DGT_BASE + TMR_ENABLE, 1 /* count, free running */);
    mmio_write32(CSR_BASE, DGT_BASE + TMR_CLEAR, 1);
    return mmio_read32(CSR_BASE, DGT_BASE + TMR_COUNT);   /* the clear's zero */
}

static void wait_ticks(u32 ticks)
{
    u32 t0 = dgt_start();

    while (mmio_read32(CSR_BASE, DGT_BASE + TMR_COUNT) - t0 < ticks) {
        /* wait for the counter to catch up */
    }
}

/* Delays up to a few minutes; past that the tick count would wrap. */
void udelay(unsigned us)
{
    u32 ticks = us * TICKS_PER_US_NUM / TICKS_PER_US_DEN;

    if (ticks) {
        wait_ticks(ticks);
    }
}

void mdelay(unsigned ms)
{
    u32 ticks = ms * TICKS_PER_MS;

    if (ticks) {
        wait_ticks(ticks);
    }
}