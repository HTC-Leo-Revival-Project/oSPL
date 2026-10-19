/*
 * gpio.c - the GPIO banks, from arch/arm/mach-msm/gpio_hw.h.
 *
 * There are two of them and the banks are not laid out the same in each, so
 * the register offsets below are per bank rather than a base plus a stride.
 * Bank 1 - which is where the keypad is - lives in GPIO2, everything else in
 * GPIO1, and the register offsets differ between them: bank 1's output
 * enable is at +0x08 where bank 0 has it at +0x20.
 *
 * Each bank has three registers that matter here:
 *
 *   OUT   what an output pin is driven to
 *   OE    which direction each pin is; 1 = output
 *   IN    what each pin reads: the driven level if it is an output, the
 *         outside world's if it is not
 *
 * A pin is a bit in a bank, and the bit is the pin number less the first pin
 * the bank covers.
 */
#include "spl.h"
#include "board.h"
#include "gpio.h"

struct bank {
    u32 first;              /* the first pin number this bank covers       */
    u32 last;
    u32 base;               /* where the bank's registers live            */
    u32 out;
    u32 oe;
    u32 in_;
};

static const struct bank banks[] = {
    {   0,  15, GPIO1_BASE + 0x800, 0x00, 0x20, 0x50 },
    {  16,  42, GPIO2_BASE + 0xc00, 0x00, 0x08, 0x20 },
    {  43,  67, GPIO1_BASE + 0x800, 0x04, 0x24, 0x54 },
    {  68,  94, GPIO1_BASE + 0x800, 0x08, 0x28, 0x58 },
    {  95, 103, GPIO1_BASE + 0x800, 0x0c, 0x2c, 0x5c },
    { 104, 121, GPIO1_BASE + 0x800, 0x10, 0x30, 0x60 },
    { 122, 152, GPIO1_BASE + 0x800, 0x14, 0x34, 0x64 },
    { 153, 164, GPIO1_BASE + 0x800, 0x18, 0x38, 0x68 },
};

#define NBANKS (sizeof(banks) / sizeof(banks[0]))

/*
 * The bank and the bit for a pin, or 0 if the pin is not one of ours.  There
 * is nothing sensible to do with a pin that does not exist, and every caller
 * here has the number from a table it wrote down, so a silent no-op is kinder
 * than a fault in the middle of the boot.
 */
static const struct bank *bank_of(u32 pin)
{
    for (u32 i = 0; i < NBANKS; i++) {
        if (pin >= banks[i].first && pin <= banks[i].last) {
            return &banks[i];
        }
    }
    return NULL;
}

/*
 * Set or clear one bit of a bank's register.  Read, change, write rather than
 * anything cleverer: the banks are one register wide, the only reason to go
 * via the GPIO at all is that each bank is a whole register.
 */
static void bank_modify(const struct bank *b, u32 reg, u32 bit, int level)
{
    volatile u32 *r = (volatile u32 *)(u32)(b->base + reg);

    *r = level ? (*r | (1u << bit)) : (*r & ~(1u << bit));
}

void gpio_output(u32 pin)
{
    const struct bank *b = bank_of(pin);

    if (b) {
        bank_modify(b, b->oe, pin - b->first, 1);
    }
}

void gpio_write(u32 pin, int level)
{
    const struct bank *b = bank_of(pin);

    if (b) {
        bank_modify(b, b->out, pin - b->first, level);
    }
}

int gpio_read(u32 pin)
{
    const struct bank *b = bank_of(pin);

    if (!b) {
        return 0;
    }
    return (*(volatile u32 *)(u32)(b->base + b->in_) >> (pin - b->first)) & 1;
}

void gpio_set_nand_cs(int level)
{
    gpio_output(GPIO_NAND_CS);
    gpio_write(GPIO_NAND_CS, level);
}