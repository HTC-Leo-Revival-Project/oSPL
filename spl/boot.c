/*
 * boot.c - the hand-off to the OS image.
 *
 * Everything we have written has gone through the data cache, so the last
 * thing to do before another core starts fetching from the load address is
 * make sure it sees all of it.  Then translation off, so that the address we
 * jump to is the physical address it is and the image's own relocations run
 * against the map it expects to find.
 */
#include "spl.h"
#include "boot.h"

static inline u32 read_sctlr(void)
{
    u32 v;

    __asm__ volatile("mrc p15, 0, %0, c1, c0, 0" : "=r"(v));
    return v;
}

static inline void write_sctlr(u32 v)
{
    __asm__ volatile("mcr p15, 0, %0, c1, c0, 0" :: "r"(v));
}

static inline void barrier(void)
{
    __asm__ volatile("dsb sy" ::: "memory");
}

/*
 * Write the data cache back and throw it away.
 *
 * The boot ROM hands us the core with its caches on, and between the console
 * and the hand-over this bootloader has put an OS image, a framebuffer and
 * a page of board globals through the data cache.  Whatever is still dirty
 * in it is invisible to whoever runs next - and the image in particular is
 * what they are about to execute.
 *
 * CTR says how long a line is and CLIDR how big the first-level data cache
 * is.  Walking the whole set/way space is the only way to cover a cache
 * without having to keep a list of everything that was written to it, which
 * is not a list worth keeping.
 */
static void clean_dcache(void)
{
    u32 ctr, clidr, line, sets, ways;

    if (!(read_sctlr() & (1u << 2))) {
        return;                        /* the D-cache is off already */
    }

    __asm__ volatile("mrc p15, 0, %0, c0, c0, 1" : "=r"(ctr));    /* CTR   */
    __asm__ volatile("mrc p15, 0, %0, c0, c0, 0" : "=r"(clidr));  /* CLIDR */

    line = 4u << (ctr & 0xf);                    /* CTR.DminLine       */
    sets = 1u << ((clidr >> 13) & 0x7);          /* LoC/Dcache, sets   */
    ways = 1u << ((clidr >> 16) & 0x7);          /* LoC/Dcache, ways   */

    if (!sets || !ways) {
        return;                        /* no first-level data cache here */
    }

    for (u32 w = 0; w < ways; w++) {
        for (u32 s = 0; s < sets; s += line) {
            u32 sw = (s << 5) | w;    /* DCCISW takes sets<<5 | ways */

            __asm__ volatile(
                "mcr p15, 0, %0, c7, c14, 2\n"   /* clean+invalidate  */
                "dsb sy\n"
                :: "r"(sw) : "memory");
        }
    }
    barrier();
    __asm__ volatile("isb sy" ::: "memory");
}

/* Invalidate every entry in the TLB. */
static void flush_tlb(void)
{
    __asm__ volatile(
        "mov r0, #0\n"
        "mcr p15, 0, r0, c8, c7, 0\n"      /* UTLBIALL */
        "dsb sy\n"
        "isb sy\n" ::: "r0", "memory");
}

/*
 * Turn translation off, and the caches with it.
 *
 * The I-cache is a special case: it may hold instructions fetched from the
 * load address by whatever ran there last, so it has to be invalidated
 * rather than merely switched off.  By then the whole D-cache above has been
 * thrown away too, which is the cheapest correct answer - whoever runs next
 * starts cold rather than inheriting a stale cache nobody knows the contents
 * of.
 */
static void caches_and_mmu_off(void)
{
    u32 sctlr = read_sctlr();

    /* invalidate the I-cache to the point of unification */
    __asm__ volatile(
        "mov r0, #0\n"
        "mcr p15, 0, r0, c7, c5, 0\n"      /* ICIALLU */
        "mcr p15, 0, r0, c7, c5, 6\n"      /* BXFLUSH */
        "dsb sy\n"
        "isb sy\n" ::: "r0", "memory");

    sctlr &= ~(1u << 0);                   /* M: no translation           */
    sctlr &= ~(1u << 2);                   /* C: no D-cache               */
    sctlr &= ~(1u << 12);                  /* I: no I-cache               */
    sctlr &= ~(1u << 13);                  /* V: low vectors at 0         */
    write_sctlr(sctlr);
    barrier();
    __asm__ volatile("isb sy" ::: "memory");
}

void boot_jump(u32 addr)
{
    clean_dcache();
    flush_tlb();
    caches_and_mmu_off();

    __asm__ volatile(
        "mov r0, %0\n"
        "bx r0\n"
        :: "r"(addr)
        : "r0", "memory");
    for (;;) {
        /* not reached */
    }
}