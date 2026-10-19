/*
 * crc32.h - the flash image checksum.
 */
#ifndef CRC32_H
#define CRC32_H

#include "spl.h"

/*
 * `n` bytes at `p`, continuing from `seed`.  Start at 0xffffffff and finish
 * with the result as it stands - do not invert it afterwards, because what is
 * being compared is this against itself.
 */
u32 crc32(const u8 *p, u32 n, u32 seed);

#endif /* CRC32_H */
