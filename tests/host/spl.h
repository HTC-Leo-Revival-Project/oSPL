/*
 * spl.h - the host stand-in, for testing the drivers off the machine.
 *
 * The real spl.h describes a machine that is not there: memory-mapped
 * registers, physical addresses, a linker script.  Nothing about parsing a
 * file needs any of that, and having to spin up an emulator to check that a
 * header is read correctly is a good way to never check it.
 *
 * This is the smallest thing the pure-logic parts of the drivers can be built
 * against.  It is NOT a simulator: anything that touches hardware will not
 * work here, and does not pretend to.
 */
#ifndef HOST_SPL_H
#define HOST_SPL_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;

static inline u32 read_le32(const void *p)
{
    const u8 *b = p;
    return (u32)b[0] | ((u32)b[1] << 8) | ((u32)b[2] << 16) | ((u32)b[3] << 24);
}

static inline u32 read_le16(const void *p)
{
    const u8 *b = p;
    return (u32)b[0] | ((u32)b[1] << 8);
}

#endif /* HOST_SPL_H */
