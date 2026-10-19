/*
 * gpio.h - the GPIO banks.
 */
#ifndef GPIO_H
#define GPIO_H

#include "spl.h"

/* GPIO1 at 0xa9000000 holds the pins up to 15 and everything above 42 */
#define GPIO1_BASE          0xA9000000u
#define GPIO2_BASE          0xA9100000u  /* and bank 1, pins 16..42        */

/* Make `pin` an output, leaving what it drives at zero. */
void gpio_output(u32 pin);

/* Drive an output pin. */
void gpio_write(u32 pin, int level);

/* Read a pin, whichever way it is configured. */
int gpio_read(u32 pin);

/* Configure, drive and read the NAND's chip select in one go. */
void gpio_set_nand_cs(int level);

#endif /* GPIO_H */