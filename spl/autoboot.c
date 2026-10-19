/*
 * autoboot.c - find the OS image in the flash, put it where it belongs, and
 * hand the core over to it.
 *
 * This is the last thing a bootloader does and the first thing it exists for,
 * so it lives on its own: the normal path calls it after the key test, and
 * the bootloader's own "boot" command calls it when somebody asks for it.
 * Neither of them needs to know how it works.
 */
#include "spl.h"
#include "board.h"

#include "drivers/uart/uart.h"
#include "drivers/nand/nand.h"
#include "drivers/timer/timer.h"

#include "osimage.h"
#include "boot.h"

static void stop(void)
{
    for (;;) {
    }
}

void autoboot(void)
{
    struct nand_info nand;
    struct os_image img;
    u32 clear;

    if (nand_init(&nand)) {
        uart_puts("SPL: no usable NAND flash\r\n");
        stop();
    }
    uart_printf("SPL: flash maker=0x%02x device=0x%02x, %u blocks of %u "
                "pages\r\n", nand.id.maker, nand.id.device,
                nand.num_blocks, nand.pages_per_block);

    if (osimage_locate(&img)) {
        uart_puts("SPL: no OS image to boot\r\n");
        stop();
    }
    uart_printf("SPL: OS image, %u sectors of %u, at 0x%08x\r\n",
                img.sectors, NAND_PAGE_SIZE, img.base);

    /*
     * Clear where it is going to land first.  The image can be up to ten
     * megabytes, and whatever was in RAM before is not something to hand on.
     */
    clear = img.size > OS_IMAGE_MAX ? OS_IMAGE_MAX : img.size;
    uart_printf("SPL: clearing %u bytes at 0x%08x\r\n", clear, img.base);
    memset((void *)(u32)img.base, 0xff, clear);

    uart_puts("SPL: loading\r\n");
    if (osimage_load(&img)) {
        uart_puts("SPL: read failed\r\n");
        stop();
    }
    uart_puts("SPL: loaded\r\n");

    mdelay(50);
    uart_printf("SPL: jumping to 0x%08x\r\n", img.base);
    boot_jump(img.base);
}