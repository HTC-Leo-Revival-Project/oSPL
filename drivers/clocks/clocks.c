/*
 * clocks.c - the clock controller at 0xa8600000, and the Scorpion's own PLL.
 *
 * Everything on this SoC is clocked off a 19.2 MHz crystal, and every PLL
 * multiplies it by (L + M/N).  Each one is four registers at 0x300 + 32n:
 * the mode, L, M and N.  Writing L with N = 1 and M = 0 gives whole multiples,
 * which is what the PLLs the machine comes up with are set to.
 *
 * The CPU's own PLL is somewhere else entirely: the Scorpion's is at
 * 0xa8800000, where 0x04 is the control register and 0x10 the frequency
 * selector.  It runs at 998.4 MHz, which is L = 26.
 *
 * This is the first thing the real HSPL does and it is what the rest of it
 * assumes: a machine with the clocks stopped cannot read the flash, cannot
 * drive the panel, and cannot hand over to anything.
 */
#include "spl.h"
#include "board.h"
#include "clocks.h"

#define CLK_CTL_BASE     0xA8600000u
#define SCPLL_BASE       0xA8800000u

/* PLLn: mode, L, M, N - four words, 32 bytes apart, from 0x300. */
#define PLL_BASE         0x0300u
#define PLL_MODE         0x0
#define PLL_L            0x4
#define PLL_M            0x8
#define PLL_N            0xc

#define PLL_MODE_RUN     0x7

#define SCPLL_CTL        0x04
#define SCPLL_FSM_EXT    0x10

/*
 * The five PLLs the machine runs, and what each is set to.  The two that are
 * not set are ones it leaves alone: they belong to parts of the modem, which
 * the baseband firmware looks after.
 */
static const struct {
    u8 n;
    u8 l;
} plls[] = {
    { 0, 40 },           /* MPLL,  768.0 MHz */
    { 1, 41 },           /* GPLL,  787.2 MHz */
    { 4, 20 },           /*          384.0 MHz */
    { 5, 50 },           /*          960.0 MHz */
};

#define NPLLS (sizeof(plls) / sizeof(plls[0]))

#define XTAL_HZ          19200000u

static u32 pll_regs(int n)
{
    return CLK_CTL_BASE + PLL_BASE + 32 * (u32)n;
}

void clocks_init(void)
{
    u32 i;

    for (i = 0; i < NPLLS; i++) {
        u32 base = pll_regs(plls[i].n);

        /*
         * The mode bit has to be clear while the dividers are being changed,
         * or the PLL is still running off the old ones halfway through.
         */
        mmio_write32(CLK_CTL_BASE, base + PLL_MODE, 0);
        mmio_write32(CLK_CTL_BASE, base + PLL_L, plls[i].l);
        mmio_write32(CLK_CTL_BASE, base + PLL_M, 0);
        mmio_write32(CLK_CTL_BASE, base + PLL_N, 1);
        mmio_write32(CLK_CTL_BASE, base + PLL_MODE, PLL_MODE_RUN);
    }

    /* The CPU's own PLL, which is not one of those. */
    mmio_write32(SCPLL_BASE, SCPLL_CTL, 0x00000002);   /* out of reset */
    mmio_write32(SCPLL_BASE, SCPLL_FSM_EXT, 26 << 3);
}

u32 clocks_pll(int n)
{
    for (u32 i = 0; i < NPLLS; i++) {
        if (plls[i].n == n) {
            u32 off = PLL_BASE + 32 * (u32)n;
            u32 l = mmio_read32(CLK_CTL_BASE, off + PLL_L);
            u32 m = mmio_read32(CLK_CTL_BASE, off + PLL_M);
            u32 d = mmio_read32(CLK_CTL_BASE, off + PLL_N);

            if (!d) {
                d = 1;
            }
            return XTAL_HZ * (l + m / d);
        }
    }
    return 0;
}