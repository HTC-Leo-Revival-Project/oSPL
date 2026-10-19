/*
 * bl.c - the bootloader: the screen, the prompt, and the commands.
 *
 * This is what the phone becomes instead of a way of reaching the OS.  The
 * HSPL gets here for one of three reasons - a tag the flashing tool left, a
 * tag in a flashed image, or somebody holding volume down as the phone came
 * up - and once here it draws its screen, prints a prompt, and reads lines
 * until it is told to boot.
 *
 * Everything it prints goes to the panel and to the console, because on a
 * phone there is no guarantee anyone is looking at either one.
 */
#include "spl.h"
#include "board.h"

#include "drivers/uart/uart.h"
#include "drivers/display/console.h"
#include "drivers/keypad/keypad.h"

#include "format.h"
#include "bl.h"
#include "cmdgroup.h"
#include "autoboot.h"

/* ---- colours --------------------------------------------------------- */

#define RGB(r, g, b) \
    ((u16)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))

#define C_BG        RGB(0, 0, 0)
#define C_FG        RGB(255, 255, 255)
#define C_HEAD      RGB(255, 255, 0)
#define C_OK        RGB(0, 255, 0)
#define C_BAD       RGB(255, 80, 0)
#define C_DIM       RGB(120, 120, 120)

/* The HSPL's banner colours, banded across the top of the panel. */
static const u16 banner[] = {
    RGB(255, 0, 0), RGB(255, 120, 0), RGB(255, 255, 0), RGB(0, 255, 0),
    RGB(0, 200, 255), RGB(0, 80, 255), RGB(160, 0, 255), RGB(255, 255, 255),
};
#define NBANNER (sizeof(banner) / sizeof(banner[0]))

/* ---- what we say about ourselves -------------------------------------- */

#define BL_VERSION   "2.08.HSPL"
#define BL_FLAVOUR   "8G"
#define BL_XA        "XA"

/* ---- where the "waddr" and "wdata" commands put things ---------------- */

static u32 wr_addr;

/* ---- the command table ------------------------------------------------ */

/*
 * The shape is the HSPL's: a name, a handler that gets the arguments already
 * split off, and a flags word.  The flags are a visibility lock - a command
 * marked BL_HIDDEN is not listed and is not found by name, which is how the
 * HSPL keeps the manufacturing ones out of the menu.
 */
struct bl_command {
    const char *name;
    void (*handler)(char **args, int nargs);
    u32 flags;
    const char *help;
};

/*
 * The flags are a visibility lock: a command marked BL_HIDDEN is neither
 * listed by help nor found by name, which is how the HSPL keeps the
 * manufacturing commands out of the menu.
 */
#define BL_HIDDEN    0x01

/* The other groups of commands, which the menu knows nothing about. */
extern const struct bl_flash_command bus_commands[];
extern const int bus_command_count;
extern const struct bl_flash_command misc_commands[];
extern const int misc_command_count;

static void cmd_help(char **args, int nargs);
static void cmd_blversion(char **args, int nargs);
static void cmd_info(char **args, int nargs);
static void cmd_mddir(char **args, int nargs);
static void cmd_mddiw(char **args, int nargs);
static void cmd_waddr(char **args, int nargs);
static void cmd_wdata(char **args, int nargs);
static void cmd_keypad(char **args, int nargs);
static void cmd_boot(char **args, int nargs);

static const struct bl_command commands[] = {
    { "help",       cmd_help,       0, "this list" },
    { "blversion",  cmd_blversion,  0, "the version of this bootloader" },
    { "info",       cmd_info,       0, "board, panel and memory" },
    { "mddir",      cmd_mddir,      0, "[Address] - read memory" },
    { "mddiw",      cmd_mddiw,      0, "[Address] [Data] - write memory" },
    { "waddr",      cmd_waddr,      0, "[Address] - where wdata writes" },
    { "wdata",      cmd_wdata,      0, "[Data] - write a word" },
    { "keypad",     cmd_keypad,     0, "what is pressed right now" },
    { "boot",       cmd_boot,       0, "start the OS" },
};

#define NCOMMANDS (sizeof(commands) / sizeof(commands[0]))



/* ---- output ---------------------------------------------------------- */

void bl_say(const char *s)
{
    console_puts(s);
    uart_puts(s);
}

/*
 * The bootloader talks to both at once: the panel is where the person is, and
 * the console is where whatever is reading the log is.
 */
static void bl_emit(char c, void *arg)
{
    (void)arg;
    console_putc(c);
    uart_putc(c);
}

void bl_printf(const char *fmt, ...)
{
    __builtin_va_list ap;

    __builtin_va_start(ap, fmt);
    spl_vformat(bl_emit, NULL, fmt, ap);
    __builtin_va_end(ap);
}

/* ---- the commands ---------------------------------------------------- */

static void cmd_help(char **args, int nargs)
{
    u32 i;

    (void)args;
    (void)nargs;
    bl_printf("Commands:\n");
    for (i = 0; i < NCOMMANDS; i++) {
        if (commands[i].flags & BL_HIDDEN) {
            continue;
        }
        bl_printf("  %-12s %s\n", commands[i].name,
                  commands[i].help ? commands[i].help : "");
    }
    for (i = 0; i < (u32)flash_command_count; i++) {
        bl_printf("  %-12s %s\n", flash_commands[i].name,
                  flash_commands[i].help);
    }
    for (i = 0; i < (u32)bus_command_count; i++) {
        bl_printf("  %-12s %s\n", bus_commands[i].name,
                  bus_commands[i].help);
    }
    for (i = 0; i < (u32)misc_command_count; i++) {
        bl_printf("  %-12s %s\n", misc_commands[i].name,
                  misc_commands[i].help);
    }
}

static void cmd_blversion(char **args, int nargs)
{
    (void)args;
    (void)nargs;
    bl_printf("%s %s %s\n", BL_VERSION, BL_FLAVOUR, BL_XA);
}

static void cmd_info(char **args, int nargs)
{
    (void)args;
    (void)nargs;
    bl_printf("Board ID is %u\n", (u32)BOARD_ID);
    bl_printf("Panel_TypeDetect: %u\n", (u32)PANEL_TYPE_SHARP);
    bl_printf("Memory base 0x%08x, %u KB\n", (u32)RAM_BASE,
              (u32)(RAM_END / 1024));
}

/*
 * mddir [Address] - read memory.  The HSPL calls this "memory display"; the
 * argument is the one word at that address and nothing else, which is what
 * makes it a debugger rather than a hex dump.
 */
static void cmd_mddir(char **args, int nargs)
{
    u32 addr;

    if (nargs != 2) {
        bl_printf("CMD>mddir [Address]\n");
        return;
    }
    addr = (u32)bl_number(args[1]);
    bl_printf("0x%08x = 0x%08x\n", addr, *(volatile u32 *)(u32)addr);
}

static void cmd_mddiw(char **args, int nargs)
{
    u32 addr, data;

    if (nargs != 3) {
        bl_printf("CMD>mddiw [Address] [Data]\n");
        return;
    }
    addr = (u32)bl_number(args[1]);
    data = (u32)bl_number(args[2]);
    *(volatile u32 *)(u32)addr = data;
    bl_printf("0x%08x <- 0x%08x\n", addr, data);
}

/* waddr [Address] - remember an address for wdata to write to. */
static void cmd_waddr(char **args, int nargs)
{
    if (nargs != 2) {
        bl_printf("CMD>waddr [Address]\n");
        return;
    }
    wr_addr = (u32)bl_number(args[1]);
    bl_printf("address 0x%08x\n", wr_addr);
}

/* wdata [Data] - write a word to the address waddr was given. */
static void cmd_wdata(char **args, int nargs)
{
    u32 data;

    if (nargs != 2) {
        bl_printf("CMD>wdata [Data]\n");
        return;
    }
    data = (u32)bl_number(args[1]);
    *(volatile u32 *)(u32)wr_addr = data;
    bl_printf("0x%08x <- 0x%08x\n", wr_addr, data);
}

/* keypad - dump the matrix, the way the HSPL's does. */
static void cmd_keypad(char **args, int nargs)
{
    static const char *const names[] = {
        "volup", "voldown", "windows", "call", "endcall", "back", "home"
    };
    u32 down = keypad_scan();
    int n = 0;

    (void)args;
    (void)nargs;
    for (int i = 0; i < KEY_COUNT; i++) {
        if (down & (1u << i)) {
            n++;
        }
    }
    bl_printf("PressedKeyNum = (%d)\n", n);
    bl_printf("dwMatrixC3_0_Status = (0x%x)\n",
              (down & 0x7) ? 1 : 0);
    bl_printf("dwMatrixC7_4_Status = (0x%x)\n",
              (down & (7u << 4)) ? 1 : 0);
    for (int i = 0; i < KEY_COUNT; i++) {
        if (down & (1u << i)) {
            bl_printf("  %s\n", names[i]);
        }
    }
}

/* boot - do what would have been done, and go. */
static void cmd_boot(char **args, int nargs)
{
    (void)args;
    (void)nargs;
    bl_printf("booting\n");
    autoboot();
}

/* ---- running a line -------------------------------------------------- */

u32 bl_number(const char *s)
{
    u32 v = 0;

    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
        for (; *s; s++) {
            v <<= 4;
            if (*s >= '0' && *s <= '9') {
                v |= *s - '0';
            } else if (*s >= 'a' && *s <= 'f') {
                v |= *s - 'a' + 10;
            } else if (*s >= 'A' && *s <= 'F') {
                v |= *s - 'A' + 10;
            } else {
                break;
            }
        }
        return v;
    }
    for (; *s >= '0' && *s <= '9'; s++) {
        v = v * 10 + (*s - '0');
    }
    return v;
}

static int is_space(char c)
{
    return c == ' ' || c == '\t';
}

/*
 * Split a line into arguments in place.  The HSPL counts the command word as
 * argument zero, so `mddir 0x80000` is two arguments and that is what every
 * handler's nargs check is written against.
 */
static int split(char *line, char **argv, int max)
{
    int n = 0;

    while (*line && n < max) {
        while (is_space(*line)) {
            line++;
        }
        if (!*line) {
            break;
        }
        argv[n++] = line;
        while (*line && !is_space(*line)) {
            line++;
        }
        if (*line) {
            *line++ = '\0';
        }
    }
    return n;
}

static void run(char *line)
{
    char *argv[BL_MAX_ARGS];
    int n = split(line, argv, BL_MAX_ARGS);
    u32 i;

    if (n == 0) {
        return;
    }
    for (i = 0; i < (u32)flash_command_count; i++) {
        if (bl_streq(argv[0], flash_commands[i].name)) {
            flash_commands[i].handler(argv, n);
            return;
        }
    }
    for (i = 0; i < (u32)bus_command_count; i++) {
        if (bl_streq(argv[0], bus_commands[i].name)) {
            bus_commands[i].handler(argv, n);
            return;
        }
    }
    for (i = 0; i < (u32)misc_command_count; i++) {
        if (bl_streq(argv[0], misc_commands[i].name)) {
            misc_commands[i].handler(argv, n);
            return;
        }
    }
    for (i = 0; i < NCOMMANDS; i++) {
        if (commands[i].flags & BL_HIDDEN) {
            continue;
        }
        if (bl_streq(argv[0], commands[i].name)) {
            commands[i].handler(argv, n);
            return;
        }
    }
    bl_printf("Unknown command: %s\n", argv[0]);
    bl_printf("Try 'help'\n");
}

int bl_streq(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

/* ---- the screen ------------------------------------------------------ */

static void banner_row(int y)
{
    int band = PANEL_WIDTH / (int)NBANNER;
    int x;

    for (x = 0; x < (int)PANEL_WIDTH; x++) {
        int i = x / band;

        ((u16 *)(u32)FB_BASE)[y * PANEL_WIDTH + x] = banner[i % NBANNER];
    }
}

static void draw_screen(void)
{
    int i;

    screen_fill(C_BG);
    for (i = 0; i < 8; i++) {
        banner_row(i);
    }

    text_at(8, 12, C_OK, "Security Unlocked");
    text_at(8, 26, C_FG, "PB81100 SS-BC");
    text_at(8, 40, C_HEAD, "SPL-" BL_VERSION " " BL_FLAVOUR " " BL_XA);

    text_at(8, 62, C_DIM, "Serial  USB  Debug");

    console_init();
    console_goto(0, 10);
    bl_printf("\nPress 'help' for the command list.\n");
}

/* ---- reading a line -------------------------------------------------- */

/*
 * One character at a time, echoed to the panel and the console as it arrives.
 * That is the HSPL's behaviour and it is the right one: a terminal that only
 * showed what you had finished typing would be no use on a phone.
 */
static int read_line(char *buf, int max)
{
    int n = 0;

    for (;;) {
        int c = uart_getc();

        if (c < 0) {
            continue;
        }
        if (c == '\r' || c == '\n') {
            bl_printf("\n");
            buf[n] = '\0';
            return n;
        }
        if (c == 8 || c == 127) {          /* backspace */
            if (n) {
                n--;
                bl_printf("\b \b");
            }
            continue;
        }
        if (c < 32 || n + 1 >= max) {
            continue;
        }
        buf[n++] = (char)c;
        bl_printf("%c", c);
    }
}

void bl_enter(void)
{
    char line[BL_LINE_MAX];

    draw_screen();

    for (;;) {
        bl_printf("BL> ");
        if (read_line(line, BL_LINE_MAX)) {
            run(line);
        }
    }
}