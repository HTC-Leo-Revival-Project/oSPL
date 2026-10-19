/*
 * bus.c - the commands that talk to things on a bus.
 *
 * The I2C commands are the HSPL's own, and they are the reason a bootloader
 * is more than a way of loading an OS: the EEPROM that says which board this
 * is, the panel over its own link, the backlight and the audio codec all come
 * off a bus, and every one of them is something to poke at when the thing is
 * not working.
 */
#include "spl.h"
#include "board.h"

#include "drivers/i2c/i2c.h"
#include "drivers/timer/timer.h"
#include "bl.h"
#include "cmdgroup.h"

/*
 * The HSPL's names for what is on the bus, so the same numbers work.
 *
 * Its commands take the address the way it is written on a schematic, with
 * the direction bit still attached - 0xcc for the proximity controller rather
 * than 0x66 - so that is what these take too, and the shift to the seven bits
 * the controller wants happens here.
 */
static const char *slave_name(u32 addr)
{
    switch (addr >> 1) {
    case I2C_AKM_CODEC:   return "akm codec";
    case I2C_TOUCHSCREEN: return "touchscreen";
    case I2C_GAUGE:       return "gauge";
    case I2C_MICROP:      return "microp";
    default:              return "unknown";
    }
}

/* i2cr [SlaveID] [RegAddr] [NumberOfByte] - read registers. */
static void cmd_i2cr(char **args, int nargs)
{
    u32 slave, reg, n;

    if (nargs != 4) {
        bl_printf("CMD>i2cr [SlaveID] [RegAddr] [NumberOfByte]\n");
        return;
    }
    slave = bl_number(args[1]);
    reg = bl_number(args[2]);
    n = bl_number(args[3]);

    bl_printf("i2cr %s 0x%02x reg 0x%02x, %u bytes\n", slave_name(slave), slave,
              reg, n);
    for (u32 base = 0; base < n; base += 16) {
        u32 want = n - base < 16 ? n - base : 16;
        u8 buf[16];

        if (i2c_read(slave, reg + base, buf, want)) {
            bl_printf("  I2C_Read error at 0x%02x\n", reg + base);
            return;
        }
        bl_printf("0x%02x:", reg + base);
        for (u32 i = 0; i < want; i++) {
            bl_printf(" %02x", buf[i]);
        }
        bl_printf("\n");
    }
}

/* i2cw [SlaveID] [RegAddr] [NumberOfByte] [Data...] - write registers. */
static void cmd_i2cw(char **args, int nargs)
{
    u32 slave, reg, n;
    u8 buf[16];

    if (nargs < 5) {
        bl_printf("CMD>i2cw [SlaveID] [RegAddr] [NumberOfByte] [Data...]\n");
        return;
    }
    slave = bl_number(args[1]);
    reg = bl_number(args[2]);
    n = bl_number(args[3]);

    if (n > sizeof(buf) || n + 4 != (u32)nargs) {
        bl_printf("Invalid input parameters!\n");
        return;
    }
    for (u32 i = 0; i < n; i++) {
        buf[i] = (u8)bl_number(args[4 + i]);
    }

    bl_printf("i2cw %s 0x%02x reg 0x%02x, %u bytes\n", slave_name(slave),
              slave, reg, n);
    if (i2c_write(slave, reg, buf, n)) {
        bl_printf("  I2C_Write error\n");
        return;
    }
    bl_printf("ok\n");
}

/*
 * i2cr2 [SlaveID] [NumberOfByte] - read from wherever the device is pointing.
 *
 * The other read takes a register; this one does not, which is how you find
 * out where a device has left its pointer.
 */
static void cmd_i2cr2(char **args, int nargs)
{
    u32 slave, n;
    u8 buf[16];

    if (nargs != 3) {
        bl_printf("CMD>i2cr2 [SlaveID] [NumberOfByte]\n");
        return;
    }
    slave = bl_number(args[1]);
    n = bl_number(args[2]);

    if (n > sizeof(buf)) {
        bl_printf("Invalid input parameters!\n");
        return;
    }
    bl_printf("i2cr2 %s 0x%02x, %u bytes\n", slave_name(slave), slave, n);
    if (i2c_read(slave, 0, buf, n)) {
        bl_printf("  I2C_Read error\n");
        return;
    }
    for (u32 i = 0; i < n; i++) {
        bl_printf(" %02x", buf[i]);
    }
    bl_printf("\n");
}

/*
 * micropchecksum [APCodeVersion] [CheckSumValue] [MicroP ID] - read the
 * proximity controller's version and checksum.
 *
 * This is the device behind the HSPL's proximitytest and dcompasstest: it is
 * the same bus, one address away from the gauge.
 */
static void cmd_micropchecksum(char **args, int nargs)
{
    u8 ver[2];

    (void)args;
    (void)nargs;

    if (i2c_read(I2C_MICROP, 0x30, ver, sizeof(ver))) {
        bl_printf("microp: not answering\n");
        return;
    }
    bl_printf("AP version 0x%x, checksum 0x%x\n", ver[0], ver[1]);
}

/* proximitytest [DebounceTime] [NumofCycle] - watch the proximity sensor. */
static void cmd_proximitytest(char **args, int nargs)
{
    u32 cycles = nargs > 2 ? bl_number(args[2]) : 10;

    bl_printf("proximity: %u cycles\n", cycles);
    for (u32 i = 0; i < cycles; i++) {
        u8 v;

        if (i2c_read(I2C_MICROP, 0x82, &v, 1)) {
            bl_printf("  lost the device at cycle %u\n", i);
            return;
        }
        bl_printf("  cycle %u: 0x%02x\n", i, v);
        mdelay(50);
    }
    bl_printf("done\n");
}

const struct bl_flash_command bus_commands[] = {
    { "i2cr",            cmd_i2cr,            " [Slave] [Reg] [N] - read" },
    { "i2cw",            cmd_i2cw,            " [Slave] [Reg] [N] [Data...] - write" },
    { "i2cr2",           cmd_i2cr2,           " [Slave] [N] - read, no register" },
    { "micropchecksum",  cmd_micropchecksum,  " - the proximity controller's version" },
    { "proximitytest",   cmd_proximitytest,   " [Debounce] [Cycles] - watch it" },
};

const int bus_command_count =
    (int)(sizeof(bus_commands) / sizeof(bus_commands[0]));