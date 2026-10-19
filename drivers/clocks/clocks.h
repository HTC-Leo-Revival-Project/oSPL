/*
 * clocks.h - the clock controller.
 */
#ifndef CLOCKS_H
#define CLOCKS_H

#include "spl.h"

/*
 * Bring the PLLs up and leave them running, which is the state everything
 * else on this SoC expects to find the machine in.
 */
void clocks_init(void);

/* What one of the PLLs is set to, in hertz, for the "info" command. */
u32 clocks_pll(int n);

#endif /* CLOCKS_H */