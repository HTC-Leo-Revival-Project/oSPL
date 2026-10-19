/*
 * crc32.c - the CRC that flash images are checked with.
 *
 * It is the usual reflected CRC-32 (polynomial 0xedb88320), running over the
 * bytes in order with the result left in the register, which is what a flash
 * image's trailing checksum turns out to be on this part.
 *
 * It lives on its own rather than inside the flash commands because the NBH
 * driver needs it too: to say whether the area that was just written is the
 * area the file described, which is the only way to tell a programmed flash
 * from a plausible one.
 */
#include "spl.h"
#include "crc32.h"

#define CRC32_POLY 0xedb88320u

u32 crc32(const u8 *p, u32 n, u32 seed)
{
    u32 crc = seed;

    for (u32 i = 0; i < n; i++) {
        crc ^= p[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc >> 1) ^ (CRC32_POLY & (u32) - (int)(crc & 1));
        }
    }
    return crc;
}
