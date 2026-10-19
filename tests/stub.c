/*
 * stub.c - the parts of the machine that are not there.
 *
 * Everything the NBH parser touches that is not the file itself: the UART it
 * prints breadcrumbs on, and the flash it reads back.  The flash stub is a
 * plain array, so the "write it and read it back" path can be tested without
 * an emulator.
 */
#include "spl.h"
#include "board.h"
#include "drivers/nand/nand.h"
#include "drivers/uart/uart.h"

#define NAND_BLOCKS 4096

static u8 flash[NAND_BLOCKS * NAND_BLOCK_SIZE];
static u8 erased[NAND_BLOCKS];
int verbose_flash;

#include <stdarg.h>

void uart_puts(const char *s)
{
    if (verbose_flash) {
        fputs(s, stdout);
    }
}

void uart_printf(const char *fmt, ...)
{
    va_list ap;

    if (!verbose_flash) {
        return;
    }
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
}

int nand_erase_block(u32 block)
{
    if (block >= NAND_BLOCKS) {
        return -1;
    }
    memset(flash + (size_t)block * NAND_BLOCK_SIZE, 0xff, NAND_BLOCK_SIZE);
    erased[block] = 1;
    return 0;
}

int nand_write_page(u32 page, const void *src)
{
    u32 block = page / NAND_PAGES_PER_BLK;

    if (block >= NAND_BLOCKS) {
        return -1;
    }
    /* a NAND can only clear bits, and only after an erase */
    if (!erased[block]) {
        return -1;
    }
    {
        u8 *p = flash + (size_t)page * NAND_PAGE_SIZE;
        const u8 *s = src;
        int i;

        for (i = 0; i < (int)NAND_PAGE_SIZE; i++) {
            p[i] &= s[i];
        }
    }
    return 0;
}

int nand_read_page(u32 page, void *dst)
{
    if (page >= NAND_BLOCKS * NAND_PAGES_PER_BLK) {
        return -1;
    }
    memcpy(dst, flash + (size_t)page * NAND_PAGE_SIZE, NAND_PAGE_SIZE);
    return 0;
}
