/*
 * i2c.h - the I2C bus.
 */
#ifndef I2C_H
#define I2C_H

#include "spl.h"

/*
 * Read `n` bytes from `reg` on the device at `slave`, or write them.  `slave`
 * is the 7-bit address, which is what every caller has written down.
 *
 * Return 0 on success, negative if nobody answered: the bus records a NACK
 * and there is no point carrying on with a device that is not there.
 */
int i2c_read(u32 slave, u32 reg, void *buf, u32 n);
int i2c_write(u32 slave, u32 reg, const void *buf, u32 n);

/* The devices that answer on this bus. */
#define I2C_AKM_CODEC   0x1c
#define I2C_TOUCHSCREEN 0x15
#define I2C_GAUGE       0x26
#define I2C_MICROP      0x66

#endif /* I2C_H */