/*
 * keypad.h - the key matrix.
 */
#ifndef KEYPAD_H
#define KEYPAD_H

#include "spl.h"

/*
 * The keys, in the order they come out of a scan.  The power key is not on
 * the matrix at all - it goes straight to the PMIC - so it is not here.
 */
enum keypad_key {
    KEY_VOLUP,
    KEY_VOLDOWN,
    KEY_WINDOWS,
    KEY_CALL,
    KEY_ENDCALL,
    KEY_BACK,
    KEY_HOME,
    KEY_COUNT
};

/* Take the three column pins off whatever they were and make them outputs. */
void keypad_init(void);

/*
 * Which keys are down right now, as one bit per keypad_key.  Scans one column
 * at a time, so it takes a moment and settles the matrix first.
 */
u32 keypad_scan(void);

/* True if `key` has been down for at least `ms` milliseconds. */
int keypad_held(int key, unsigned ms);

#endif /* KEYPAD_H */