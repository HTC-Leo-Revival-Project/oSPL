/*
 * spl.h - the handful of types and helpers a freestanding ARM bootloader
 * needs.  There is no libc here and there is no MMU: the boot ROM hands us
 * the core in SVC mode at physical 0 with translation off, and that is how we
 * run to the end, so every address in this tree is a physical one.
 */
#ifndef SPL_H
#define SPL_H

typedef unsigned char      u8;
typedef unsigned short     u16;
typedef unsigned int       u32;
typedef signed int         s32;
typedef unsigned long long u64;

#define NULL ((void *)0)

#define MMIO(base, off) ((volatile u8 *)(u32)(base + (off)))

/*
 * The core is little-endian and everything here is too, but say so: the NAND
 * data comes out of the controller in device order and gets swapped to the
 * core's on the way in.
 */
static inline void io_barrier(void)
{
    __asm__ volatile("dsb sy" ::: "memory");
}

static inline void io_wmb(void)
{
    __asm__ volatile("dmb sy" ::: "memory");
}

static inline u32 mmio_read32(u32 base, u32 off)
{
    return *(volatile u32 *)(base + off);
}

static inline void mmio_write32(u32 base, u32 off, u32 val)
{
    *(volatile u32 *)(base + off) = val;
}

static inline u8 mmio_read8(u32 base, u32 off)
{
    return *(volatile u8 *)(base + off);
}

static inline void mmio_write8(u32 base, u32 off, u8 val)
{
    *(volatile u8 *)(base + off) = val;
}

static inline u32 readl(const void *p)
{
    return *(const volatile u32 *)p;
}

static inline void writel(void *p, u32 v)
{
    *(volatile u32 *)p = v;
}

/*
 * A 32-bit little-endian word at an address that is not necessarily aligned.
 *
 * Not a luxury.  Two reasons, and the second is the one that bites:
 *
 *  - The partition table in an MBR starts at offset 0x1BE, so StartSector
 *    lands at 0x1C6 and TotalSectors at 0x1CA - two bytes into a word.
 *  - We run with translation off, which leaves every address as the default
 *    memory type, and on this SoC the default is Device rather than Normal.
 *    Device memory goes out over a bus that does not do unaligned transfers,
 *    so the core takes an unaligned LDR as a fault rather than quietly
 *    assembling it out of two halves the way it would on Normal memory.
 *
 * The volatile is what makes this stick: a plain read of four bytes is an
 * idiom the compiler is free to fold back into one aligned word load.
 */
static inline u32 read_le32(const void *p)
{
    const volatile u8 *b = (const volatile u8 *)p;

    return (u32)b[0] | ((u32)b[1] << 8) | ((u32)b[2] << 16) |
           ((u32)b[3] << 24);
}

/*
 * The same for two bytes, and for the same reason.
 *
 * A BPB field at an odd offset - the byte count at 0x0b, the FAT count at 0x10,
 * the partition table's first entry at 0x1be - is exactly where a compiler
 * takes "assemble two bytes into a halfword" as a halfword load.  With
 * translation off that is a data abort, not a read.
 */
static inline u32 read_le16(const void *p)
{
    const volatile u8 *b = (const volatile u8 *)p;

    return (u32)b[0] | ((u32)b[1] << 8);
}

void *memcpy(void *dst, const void *src, unsigned long n);
void *memmove(void *dst, const void *src, unsigned long n);
void *memset(void *dst, int c, unsigned long n);
int memcmp(const void *a, const void *b, unsigned long n);

/* Busy-wait `ns` nanoseconds using the digital-quantiser timer. */
void udelay(unsigned us);
void mdelay(unsigned ms);

#endif /* SPL_H */
