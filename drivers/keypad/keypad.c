/*
 * keypad.c - the 3x3 key matrix.
 *
 * board-htcleo-keypad.c has it as three columns driven from the SoC and three
 * rows read back from it, one key at each crossing.  A pressed key connects
 * its row to its column, so the way to ask whether a key is down is to drive
 * one column low, leave the others high, and read the rows: a row that has
 * come low has a key in the column being driven.
 *
 * That is a scan, one column per pass, and it has to be repeated: a key that
 * has just gone down does not read back the instant the column changes, and
 * a key that has just come up still reads down for a pass or two.
 */
#include "spl.h"
#include "board.h"
#include "gpio.h"
#include "keypad.h"
#include "drivers/timer/timer.h"

/* Column pins: driven by us, one at a time. */
#define KP_MKOUT0  33
#define KP_MKOUT1  32
#define KP_MKOUT2  31

/* Row pins: read by us. */
#define KP_MPIN0   42
#define KP_MPIN1   41
#define KP_MPIN2   40

static const u32 cols[3] = { KP_MKOUT0, KP_MKOUT1, KP_MKOUT2 };
static const u32 rows[3] = { KP_MPIN0, KP_MPIN1, KP_MPIN2 };

/* Which key sits at each crossing: [column][row]. */
static const int key_at[3][3] = {
    { KEY_VOLUP,    KEY_VOLDOWN, -1 },      /* column 0 */
    { KEY_WINDOWS,  KEY_CALL,    KEY_ENDCALL },
    { KEY_BACK,     KEY_HOME,    -1 },      /* column 2 */
};

/* Long enough for a row to have followed the column, in microseconds. */
#define SETTLE_US  100u

static void drive_column(int col)
{
    for (int c = 0; c < 3; c++) {
        gpio_write(cols[c], c != col);
    }
    udelay(SETTLE_US);
}

void keypad_init(void)
{
    for (int c = 0; c < 3; c++) {
        gpio_output(cols[c]);
        gpio_write(cols[c], 1);          /* nothing selected while we set up */
    }
    for (int r = 0; r < 3; r++) {
        (void)rows[r];                   /* inputs; nothing to configure   */
    }
    udelay(SETTLE_US);
}

u32 keypad_scan(void)
{
    u32 down = 0;

    for (int c = 0; c < 3; c++) {
        drive_column(c);
        for (int r = 0; r < 3; r++) {
            if (gpio_read(rows[r]) == 0) {
                int k = key_at[c][r];

                if (k >= 0) {
                    down |= 1u << k;
                }
            }
        }
    }

    drive_column(-1);                    /* leave the matrix idle */
    return down;
}

int keypad_held(int key, unsigned ms)
{
    u32 want = 1u << key;
    unsigned waited = 0;

    /*
     * Two scans before believing it.  The first one after a column changes is
     * not worth anything, and a bootloader that reads one bounce and decides
     * the phone came up wrong is worse than one that takes a moment longer.
     */
    if ((keypad_scan() & want) == 0) {
        return 0;
    }
    while (waited < ms) {
        mdelay(10);
        waited += 10;
        if ((keypad_scan() & want) == 0) {
            return 0;
        }
    }
    return 1;
}