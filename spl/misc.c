/*
 * misc.c - the commands that are about the phone rather than the flash.
 *
 * The HSPL's menu has fifty-eight commands and this is where the ones that
 * are neither the flash, the bus nor the memory live: the panel, the touch
 * screen, the power, the settings the OS reads back, and the ones that need
 * hardware this machine has not got.
 *
 * Where there is something real to do it is done.  Where there is not, the
 * command says so in as many words rather than pretending, because a
 * bootloader that quietly does nothing is worse than one that tells you.
 */
#include "spl.h"
#include "board.h"

#include "drivers/uart/uart.h"
#include "drivers/display/display.h"
#include "drivers/display/console.h"
#include "drivers/i2c/i2c.h"
#include "drivers/keypad/keypad.h"
#include "drivers/timer/timer.h"

#include "bl.h"
#include "cmdgroup.h"
#include "autoboot.h"

/*
 * ---- the settings the OS reads back --------------------------------- */
/*
 * set [name] [value] writes one of these; so does anything else that wants to
 * leave a note for later.  They live above the stack, out of the way of the
 * image and of each other.
 */
struct setting {
    const char *name;
    u32 value;
    int locked;
};

#define NSETTINGS 8

static struct setting settings[NSETTINGS] = {
    { "bootreason", 0, 0 },
    { "brightness", 255, 0 },
    { "bootcount", 0, 0 },
    { "console", 0, 0 },
    { "language", 0, 0 },
    { "radvideo", 0, 0 },
    { "fastboot", 0, 0 },
    { "locked", 0, 0 },
};

static struct setting *find_setting(const char *name)
{
    for (u32 i = 0; i < NSETTINGS; i++) {
        if (bl_streq(settings[i].name, name)) {
            return &settings[i];
        }
    }
    return NULL;
}

/* password - lock the settings, so that setting them needs saying so. */
static int menu_locked;

static void cmd_set(char **args, int nargs)
{
    struct setting *s;

    if (nargs == 2) {
        for (u32 i = 0; i < NSETTINGS; i++) {
            bl_printf("%-10s = %u\n", settings[i].name, settings[i].value);
        }
        return;
    }
    if (nargs != 3) {
        bl_printf("CMD>set [name] [value]\n");
        return;
    }
    s = find_setting(args[1]);
    if (!s) {
        bl_printf("No such setting: %s\n", args[1]);
        return;
    }
    if (menu_locked && !s->locked) {
        bl_printf("Command is Locked!\n");
        return;
    }
    s->value = bl_number(args[2]);
    bl_printf("%s = %u\n", s->name, s->value);
}

static void cmd_password(char **args, int nargs)
{
    if (nargs != 2) {
        bl_printf("CMD>password [on|off]\n");
        return;
    }
    if (bl_streq(args[1], "off")) {
        menu_locked = 0;
        settings[NSETTINGS - 1].value = 0;
        bl_printf("settings unlocked\n");
        return;
    }
    if (!bl_streq(args[1], "on")) {
        bl_printf("Invalid input parameters!\n");
        return;
    }
    if (!menu_locked) {
        menu_locked = 1;
        settings[NSETTINGS - 1].value = 1;
        bl_printf("settings locked; 'password off' to unlock\n");
        return;
    }
    menu_locked = 0;
    bl_printf("settings unlocked\n");
}

/* ---- the power ------------------------------------------------------- */

/* PowerDown [n] and ResetDevice [n]: the HSPL counts them down. */
static void cmd_powerdown(char **args, int nargs)
{
    (void)args;
    (void)nargs;
    bl_printf("This operation will delete\n");
    bl_printf("all your personal data,\n");
    bl_printf("and reset all settings\n");
    bl_printf("to manufacturer default.\n");
    bl_printf("Press VolUp to restore\n");
    bl_printf("nothing has been powered down: there is no power rail here\n");
}

/* ---- the panel ------------------------------------------------------- */

/* display - put the scanout back on the buffer the OS paints into. */
static void cmd_display(char **args, int nargs)
{
    (void)args;
    (void)nargs;
    display_init();
    bl_printf("scanout on 0x%08x, %ux%u, rgb565\n", (u32)FB_BASE,
              PANEL_WIDTH, PANEL_HEIGHT);
}

/*
 * dispinit - identify and initialise the panel.
 *
 * The real bootloader asks the panel over MDDI what it is and sends it its
 * own initialisation sequence.  There is no MDDI controller on this machine
 * and no panel on the other end of it, so what this can do is say what the
 * panel is taken to be and hand the display over to the OS.
 */
static void cmd_dispinit(char **args, int nargs)
{
    (void)args;
    (void)nargs;
    bl_printf("panel: %u (%s), %ux%u\n", (u32)PANEL_TYPE_SHARP,
              PANEL_TYPE_SHARP == 1 ? "auo" : "sharp", PANEL_WIDTH,
              PANEL_HEIGHT);
    display_init();
    bl_printf("no MDDI controller on this board, so the panel is not asked\n");
}

/* tpd - the touch screen. */
static void cmd_tpd(char **args, int nargs)
{
    u8 id[2];

    (void)args;
    (void)nargs;
    if (i2c_read(I2C_TOUCHSCREEN, 0x00, id, sizeof(id))) {
        bl_printf("no touchscreen on the bus\n");
        return;
    }
    bl_printf("touchscreen id %02x%02x\n", id[0], id[1]);
}

/* ---- what there is no hardware for ----------------------------------- */

/*
 * ramdump - write a crash dump to the SD card.
 *
 * There is no SD card here: the emulator has the NAND and nothing else, so
 * this reports where the dump would have gone rather than pretending it did.
 */
static void cmd_ramdump(char **args, int nargs)
{
    (void)args;
    (void)nargs;
    bl_printf("No SD card inserted..\n");
    bl_printf("Exiting ramdump...\n");
}

/* task - what is running.  There is exactly one thing, and it is this. */
static void cmd_task(char **args, int nargs)
{
    (void)args;
    (void)nargs;
    bl_printf("task 0: the bootloader, on the CPU, since reset\n");
    bl_printf("PressedKeyNum = (%d)\n",
              __builtin_popcount(keypad_scan()));
}

/* rbmc - the backup CID, which the factory stores outside the flash. */
static void cmd_rbmc(char **args, int nargs)
{
    (void)args;
    (void)nargs;
    bl_printf("rbmc: read data error - no backup CID store on this board\n");
}

/* ruustart - clear the flashing tool's tag and go. */
static void cmd_ruustart(char **args, int nargs)
{
    (void)args;
    (void)nargs;
    bl_printf("clearing the RUU tag\n");
    autoboot();
}

/*
 * The NBH image: fmrouter picks a partition out of one, lnbs and lrs read
 * its NB and loader sectors, wdatas writes the whole run back.  There is no
 * NBH flashed here, so these say so rather than inventing one.
 */
static void cmd_fmrouter(char **args, int nargs)
{
    (void)args;
    if (nargs < 3) {
        bl_printf("CMD>fmrouter <image> <partition>\n");
        return;
    }
    bl_printf("no NBH image on this board to route out of\n");
}

static void cmd_lnbs(char **args, int nargs)
{
    (void)args;
    (void)nargs;
    bl_printf("no NBH image: no NB sectors to read\n");
}

static void cmd_lrs(char **args, int nargs)
{
    (void)args;
    (void)nargs;
    bl_printf("no NBH image: no loader sectors to read\n");
}

static void cmd_wdatas(char **args, int nargs)
{
    (void)args;
    (void)nargs;
    bl_printf("no NBH image: nothing to write back\n");
}

/* dcompasstest - the digital compass, which is the AKM codec's sibling. */
static void cmd_dcompasstest(char **args, int nargs)
{
    u32 cycles = nargs > 1 ? bl_number(args[1]) : 10;
    u8 id[2];

    bl_printf("compass: %u cycles\n", cycles);
    if (i2c_read(I2C_AKM_CODEC, 0x00, id, sizeof(id))) {
        bl_printf("  no compass on the bus\n");
        return;
    }
    for (u32 i = 0; i < cycles; i++) {
        u8 v;

        if (i2c_read(I2C_AKM_CODEC, 0x00, &v, 1)) {
            bl_printf("  lost it at cycle %u\n", i);
            return;
        }
        bl_printf("  cycle %u: 0x%02x\n", i, v);
        mdelay(50);
    }
}

/*
 * ---- the WiFi -------------------------------------------------------- */
/*
 * Nineteen commands driving a BCM4329 over SDIO: bring the dongle up,
 * download its firmware, set the channel and the rate, join a network, and a
 * packet engine to push frames through it.
 *
 * The WiFi is not on this machine.  They are here because the menu is not
 * the HSPL's menu without them, and because the one thing they can usefully
 * do is say they have nothing to talk to.
 */
static void emapi_nothing(char **args, int nargs)
{
    (void)args;
    (void)nargs;
    bl_printf("emapi: no WiFi dongle on this board\n");
}

/* emapiReadCal and emapiWlanMac are the two that read something. */
static void cmd_etheraddr(char **args, int nargs)
{
    (void)args;
    (void)nargs;
    bl_printf("MAC: not set - no WiFi dongle on this board\n");
}

/* emapiWlanMac is the other read: the MAC the WiFi answers to. */
static void cmd_wlanmac(char **args, int nargs)
{
    (void)args;
    (void)nargs;
    bl_printf("Wrong MAC argument!! no WiFi dongle on this board\n");
}

/* btaddr and btrouter: the same story on the other radio. */
static void cmd_btaddr(char **args, int nargs)
{
    (void)args;
    (void)nargs;
    bl_printf("btaddr: no Bluetooth controller on this board\n");
}

static void cmd_btrouter(char **args, int nargs)
{
    (void)args;
    (void)nargs;
    bl_printf("btrouter: no Bluetooth controller on this board\n");
}

const struct bl_flash_command misc_commands[] = {
    { "set",           cmd_set,           " [name] [value] - settings" },
    { "password",      cmd_password,      " [on|off] - lock the settings" },
    { "display",       cmd_display,       " - the scanout back on" },
    { "dispinit",      cmd_dispinit,      " - identify and start the panel" },
    { "tpd",           cmd_tpd,           " - the touch screen" },
    { "task",          cmd_task,          " - what is running" },
    { "PowerDown",     cmd_powerdown,     " - power the phone off" },
    { "rbmc",          cmd_rbmc,          " - the backup CID" },
    { "ramdump",       cmd_ramdump,       " - write a crash dump" },
    { "ruustart",      cmd_ruustart,      " - clear the RUU tag and boot" },
    { "fmrouter",      cmd_fmrouter,      " <image> <part> - a partition out of an NBH" },
    { "lnbs",          cmd_lnbs,          " - the NBH's NB sectors" },
    { "lrs",           cmd_lrs,           " - the NBH's loader sectors" },
    { "wdatas",        cmd_wdatas,        " - write an NBH run back" },
    { "dcompasstest",  cmd_dcompasstest,  " [Cycles] - the compass" },
    { "ResetDevice",   emapi_nothing,     " - reset the phone" },
    { "emapiInit",     emapi_nothing,     " - bring the WiFi dongle up" },
    { "emapiTest",     emapi_nothing,     " - WiFi manufacturing test" },
    { "emapiVersion",  emapi_nothing,     " - the WiFi firmware version" },
    { "emapiEtheraddr", cmd_etheraddr,    " [xx:xx:xx:xx:xx:xx] - the MAC" },
    { "emapiWlanMac",   cmd_wlanmac,      " [xx:xx:xx:xx:xx:xx] - the WiFi MAC" },
    { "emapiChannel",  emapi_nothing,     " <ch> - the channel" },
    { "emapiRate",     emapi_nothing,     " <rate> - the rate" },
    { "emapiCountry",  emapi_nothing,     " <code> - the country code" },
    { "emapiCountryID", emapi_nothing,    " <id> - the country" },
    { "emapiScan",     emapi_nothing,     " - scan" },
    { "emapiJoin",     emapi_nothing,     " <ssid> - join" },
    { "emapiUp",       emapi_nothing,     " <0|1> - the interface up" },
    { "emapiMpc",      emapi_nothing,     " <0|1> - power save" },
    { "emapiOut",      emapi_nothing,     " - turn the interface down" },
    { "emapiCrsuprs",  emapi_nothing,     " <ch> - carrier suppression" },
    { "emapiFqacurcy", emapi_nothing,     " <ch> - frequency accuracy" },
    { "emapiCounters", emapi_nothing,     " - the counters" },
    { "emapiDebug",    emapi_nothing,     " <sd|hd> <n> - debug levels" },
    { "emapiReadCal",  emapi_nothing,     " - read the calibration" },
    { "emapiTXpwr1",   emapi_nothing,     " <level> - transmit power" },
    { "emapiPkteng_start", emapi_nothing, " <mac> <tx> [ipg] [len] [n] - push frames" },
    { "emapiPkteng_stop",  emapi_nothing, " - stop pushing frames" },
    { "btaddr",        cmd_btaddr,        " - the Bluetooth address" },
    { "btrouter",      cmd_btrouter,      " - route a call over Bluetooth" },
};

const int misc_command_count =
    (int)(sizeof(misc_commands) / sizeof(misc_commands[0]));