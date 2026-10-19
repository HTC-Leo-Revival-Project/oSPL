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
#include "drivers/clocks/clocks.h"
#include "drivers/pmic/pmic.h"
#include "drivers/nbh/nbh.h"

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
 * The NBH image.
 *
 * These are the HSPL's names for working with a flashed image, and they all
 * need one in RAM: the file comes in with `recv`, and these read out of it.
 * The address and the length default to where `recv` leaves it.
 */
static int nbh_of(char **args, int nargs, struct nbh *nb)
{
    u32 addr = DOWNLOAD_BASE;
    u32 len = DOWNLOAD_MAX;
    const char *why = NULL;
    int r;

    if (nargs > 1 && args[1][0]) {
        addr = bl_number(args[1]);
    }
    if (nargs > 2 && args[2][0]) {
        len = bl_number(args[2]);
    }
    r = nbh_open(addr, len, nb, &why);
    if (r) {
        bl_printf("nbh: %s\n", why ? why : "no reason given");
        bl_printf("nbh: LEOIMG.nbh - FAIL\n");
    }
    return r;
}

/*
 * fmrouter [image] [length] [partition] - what the file holds.
 *
 * The HSPL uses this to route out of an image during flashing: the partition
 * numbers in its own address space are the ones named in the header, and the
 * header is what says which board they belong to.
 */
static void cmd_fmrouter(char **args, int nargs)
{
    struct nbh nb;

    if (nbh_of(args, nargs, &nb)) {
        return;
    }
    for (int i = 0; i < 6; i++) {
        char name[8];

        if (nbh_name(&nb, i, name, sizeof(name)) && name[0]) {
            bl_printf("part %d \"%s\"%s\n", i, name,
                      i < 2 ? "  (board)" : "");
        }
    }
    bl_printf("target 0x%x\n", nb.target);
    bl_printf("nbh: LEOIMG.nbh - OK\n");
}

/*
 * lnbs [image] [length] - the NB sectors of the image.
 *
 * "NB" is the factory's name for a block of the image that is written but not
 * run: the MBR that says where things are, and the tag in front of the OS.
 * They come first and they are what the flash has to be given before anything
 * else can be found in it.
 */
static void cmd_lnbs(char **args, int nargs)
{
    struct nbh nb;
    u8 sector[NBH_SECTOR];

    if (nbh_of(args, nargs, &nb)) {
        return;
    }
    if (nbh_image(&nb, nb.os_off, sector, sizeof(sector)) != sizeof(sector)) {
        bl_printf("lnbs: the MBR is not all there\n");
        return;
    }
    bl_printf("NB %u: MBR, partition %d, %u sectors from %u\n", 0u,
              nb.mbr_part, nb.part_sectors, nb.start_sector);
    if (nbh_image(&nb, nb.os_off + NBH_STRIDE, sector, 8) == 8) {
        char tag[9];

        memcpy(tag, sector, 8);
        tag[8] = '\0';
        bl_printf("NB 1: \"%s\"\n", tag);
    }
    for (u32 n = 0; n < nb.os_sectors && n < 4; n++) {
        bl_printf("NB %u: sector %u, %u bytes at +0x%x\n", n, n,
                  NBH_SECTOR, nb.os_off + n * NBH_STRIDE);
    }
}

/*
 * lrs [image] [length] - the loader's own sectors.
 *
 * The loader is what runs before the OS does: the partition entry in the MBR
 * says how many sectors it occupies, and those are the first ones of the
 * image itself rather than of the area around it.
 */
static void cmd_lrs(char **args, int nargs)
{
    struct nbh nb;
    u32 first;

    if (nbh_of(args, nargs, &nb)) {
        return;
    }
    if (nb.start_sector == 0) {
        bl_printf("lrs: the MBR names no image\n");
        return;
    }
    first = nb.os_off + nb.start_sector * NBH_STRIDE;
    bl_printf("loader: %u sectors from area sector %u\n", nb.part_sectors,
              nb.start_sector);
    bl_printf("loader: +0x%x in the file, %u bytes at 0x%08x when loaded\n",
              first, nb.part_sectors * NBH_SECTOR, OS_IMAGE_BASE);
}

/*
 * wdatas [image] [length] - write the whole run back.
 *
 * The HSPL's version of this writes one run of sectors; ours writes the whole
 * area, which is the same thing with the boundaries worked out for us.  It
 * asks first, because the block it erases is the one the phone boots from.
 */
static void cmd_wdatas(char **args, int nargs)
{
    struct nbh nb;

    if (nbh_of(args, nargs, &nb)) {
        return;
    }
    bl_printf("wdatas: %u sectors, crc 0x%08x\n", nb.os_sectors,
              nbh_file_checksum(&nb));
    bl_printf("wdatas: use 'nbhflash' to write them\n");
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

/*
 * clocks and pmic - what the machine is running at, and what the power chip
 * says about itself.  Neither is on the HSPL's menu under these names; they
 * are what 'info' grew once the clocks and the power chip were real rather
 * than assumed.
 */
static void cmd_clocks(char **args, int nargs)
{
    (void)args;
    (void)nargs;
    bl_printf("PLL0 MPLL %u Hz\n", clocks_pll(0));
    bl_printf("PLL1 GPLL %u Hz\n", clocks_pll(1));
    bl_printf("PLL4      %u Hz\n", clocks_pll(4));
    bl_printf("PLL5      %u Hz\n", clocks_pll(5));
}

static void cmd_pmic(char **args, int nargs)
{
    struct board_info info;

    (void)args;
    (void)nargs;
    pmic_identify(&info);
    if (!info.present) {
        bl_printf("pmic: nothing at 0x%02x on the bus\n", 0x48);
        bl_printf("board %u, panel %u, power-on %u (assumed)\n", info.id,
                  info.panel, info.power_on);
        return;
    }
    bl_printf("pmic version 0x%02x\n", info.version);
    bl_printf("board %u, panel %u, power-on 0x%08x\n", info.id, info.panel,
              info.power_on);
}

const struct bl_flash_command misc_commands[] = {
    { "clocks",        cmd_clocks,        " - what the PLLs are running at" },
    { "pmic",          cmd_pmic,          " - the power chip, and the board" },
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