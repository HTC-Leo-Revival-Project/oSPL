/*
 * display.c - the MDP display processor and the LCDC behind it.
 *
 * The MDP at 0xAA200000 has two halves that matter here.  The DMA engine
 * ("DMA_P") fetches a rectangular region of memory and hands it on; where it
 * goes is chosen by DMA_P_CONFIG, and pointing it at the LCDC is how a frame
 * gets onto the panel.  The LCDC then scans that stream into the glass at
 * its own rate, so everything written to the DMA_P buffer turns up on the
 * panel, one frame at a time, with no further work from us.
 *
 * Register offsets are drivers/video/msm/mdp_hw.h.
 */
#include "spl.h"
#include "board.h"
#include "display.h"

#define MDP_INTR_ENABLE     0x00020
#define MDP_INTR_STATUS     0x00024
#define MDP_INTR_CLEAR      0x00028
#define MDP_DISPLAY0_START  0x00030
#define MDP_DMA_P_START     0x00044
#define MDP_DMA_S_START     0x00048
#define MDP_HW_VERSION      0x00070

#define MDP_DMA_P_CONFIG    0x90000
#define MDP_DMA_P_SIZE      0x90004
#define MDP_DMA_P_IBUF_ADDR 0x90008
#define MDP_DMA_P_IBUF_Y_STRIDE 0x9000c
#define MDP_DMA_P_OUT_XY    0x90010

#define MDP_LCDC_EN         0xe0000

/* DMAC_OUT_SEL, bit 19: 2 = the LCDC rather than memory */
#define DMA_OUT_SEL_LCDC    (2u << 19)

/*
 * PACK_PATTERN, bits 10:8, one 2-bit field per byte of a packed pixel
 * saying which component that byte carries: 0 green, 1 blue, 2 red.  0x21
 * is RGB565 - two bytes of pattern, the third field unused.
 */
#define PACK_PATTERN_RGB565 0x21u

/* the pixel format, bits 27:26: 1 = RGB565, 2 = XRGB8888 */
#define DMA_FORMAT_RGB565   (1u << 25)

#define RGB565(r, g, b) \
    ((u16)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))

void display_init(void)
{
    /*
     * Stop the LCDC first: it re-reads the DMA_P registers every frame, so
     * changing them underneath it shows up as a torn frame.
     */
    mmio_write32(MDP_BASE, MDP_LCDC_EN, 0);

    /* where the pixels come from, and how many of them there are */
    mmio_write32(MDP_BASE, MDP_DMA_P_IBUF_ADDR, FB_BASE);
    mmio_write32(MDP_BASE, MDP_DMA_P_SIZE,
                 (PANEL_HEIGHT << 16) | PANEL_WIDTH);
    mmio_write32(MDP_BASE, MDP_DMA_P_IBUF_Y_STRIDE, FB_STRIDE);
    mmio_write32(MDP_BASE, MDP_DMA_P_OUT_XY, 0);

    /* and where they go */
    mmio_write32(MDP_BASE, MDP_DMA_P_CONFIG,
                 DMA_OUT_SEL_LCDC | (PACK_PATTERN_RGB565 << 8) |
                 DMA_FORMAT_RGB565);

    /*
     * An empty panel to come out of this: whatever was in the buffer is
     * neither ours nor anything worth showing, and the first frame is a
     * better time to find that out than to have the OS paint over it.
     */
    display_clear(RGB565(0, 0, 0));

    /*
     * Start both engines.  No interrupt is enabled: the boot ROM leaves the
     * core with IRQs masked and the vector table is not ours yet, so there is
     * nothing here to catch a vsync with.  The OS turns them on when it
     * takes the display over, which is where the vector table matters.
     */
    mmio_write32(MDP_BASE, MDP_INTR_CLEAR, 0xffffffff);
    mmio_write32(MDP_BASE, MDP_DISPLAY0_START, 0);
    mmio_write32(MDP_BASE, MDP_DMA_S_START, 1);
    mmio_write32(MDP_BASE, MDP_DMA_P_START, 1);

    mmio_write32(MDP_BASE, MDP_LCDC_EN, 1);
}

void display_clear(u16 colour)
{
    volatile u16 *fb = (volatile u16 *)(u32)FB_BASE;

    for (u32 i = 0; i < PANEL_WIDTH * PANEL_HEIGHT; i++) {
        fb[i] = colour;           /* the buffer is word aligned */
    }
}