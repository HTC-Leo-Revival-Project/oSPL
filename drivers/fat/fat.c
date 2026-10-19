/*
 * fat.c - FAT32, and finding a file on the card's root.
 *
 * Enough of FAT to open one file and read it forwards, which is all a
 * bootloader does with a card: find LEOIMG.nbh, stream it into the flash.
 * There is no delete, no rename, no second FAT kept in step, and no
 * allocation - a bootloader writes to the NAND, not to the card, and a
 * filesystem that can corrupt the one thing it is here to read is worse than
 * one that cannot write at all.
 *
 * The two partition layouts are both here because both are what cards turn up
 * with: an MBR from anything PC-era, and GPT from anything made recently.
 */
#include "spl.h"
#include "board.h"
#include "drivers/sd/sd.h"
#include "fat.h"

#define SECTOR      512u

/* FAT32 BPB, the fields this needs and where they are. */
#define BPB_BYTES_SECTOR      0x0b
#define BPB_SECTORS_CLUSTER   0x0d
#define BPB_RESERVED          0x0e
#define BPB_FATS              0x10
#define BPB_ROOT_ENTRIES      0x11
#define BPB_TOTAL_SECTORS_16  0x13
#define BPB_FAT_SECTORS_16    0x16
#define BPB_TOTAL_SECTORS_32  0x20
#define BPB_FAT_SECTORS_32    0x24
#define BPB_ROOT_CLUSTER      0x2c
#define BPB_LABEL_OFF         0x47

/* A directory entry. */
#define DIR_NAME       0x00
#define DIR_ATTR       0x0b
/*
 * The 32-byte directory entry, and its field offsets.
 *
 * The high half of the first cluster comes *before* the timestamps and the low
 * half after them, which is not where a first guess puts them; getting it
 * wrong reads the cluster out of a timestamp and the size out of the cluster.
 */
#define DIR_CTIME      0x0e        /* created, tenths, time, date          */
#define DIR_MTIME      0x16        /* modified, time and date              */
#define DIR_CLUSTER_HI 0x14        /* first cluster, high 16               */
#define DIR_CLUSTER_LO 0x1a        /* first cluster, low 16                */
#define DIR_SIZE       0x1c

#define ATTR_VOLUME_ID 0x08
#define ATTR_DIRECTORY 0x10
#define ATTR_LFN       0x0f

/* Partition types worth trying, FAT32 first. */
#define MBR_FAT32_LBA   0x0c
#define MBR_FAT32_CHS   0x0b
#define MBR_FAT16_LBA   0x06
#define MBR_FAT_LBA     0x01

static u32 part_lba;           /* where the volume starts on the card */
static u32 bytes_per_sector;
static u32 sectors_per_cluster;
static u32 fat_sectors;
static u32 root_cluster;
static char label[12];

static u8 block[SECTOR] __attribute__((aligned(4)));

const char *fat_volume_label(void)
{
    return label[0] ? label : "(none)";
}

/* One sector of the volume, by its offset within the volume. */
static int vol_sector(const struct sd_card *card, u32 n, void *dst)
{
    return sd_read_block(card, part_lba + n, dst);
}

/*
 * The FAT, one sector of it.
 *
 * Cached on the sector that was last read, because a directory walk touches
 * the same FAT sector over and over and a card read is a hundred thousand
 * instructions.  One sector of cache is enough for a root directory; a long
 * path would thrash, which is another thing this does not do.
 */
static u32 fat_cached;
static int fat_cached_valid;
static u8 fat_block[SECTOR] __attribute__((aligned(4)));

static int fat_sector_at(const struct sd_card *card, u32 n, u8 **out)
{
    if (fat_cached_valid && fat_cached == n) {
        *out = fat_block;
        return 0;
    }
    if (n >= fat_sectors) {
        return -1;
    }
    if (vol_sector(card, n, fat_block)) {
        return -1;
    }
    fat_cached = n;
    fat_cached_valid = 1;
    *out = fat_block;
    return 0;
}

/* The cluster after `cluster` in the chain, or 0 at the end of it. */
static u32 fat_next(const struct sd_card *card, u32 cluster)
{
    u32 entry;
    u32 off;
    u8 *f;

    if (cluster < 2) {
        return 0;
    }
    entry = cluster * 4;
    off = entry / SECTOR;
    if (fat_sector_at(card, off, &f)) {
        return 0;
    }
    entry %= SECTOR;
    /* the top four bits of the last word are reserved and must be dropped */
    return read_le32(f + entry) & 0x0fffffff;
}

/*
 * The sector a cluster's data starts at.
 *
 * Not simply (cluster - 2) * sectors_per_cluster: the data area does not begin
 * at the front of the volume.  It begins after the reserved sectors and after
 * every copy of the FAT, and forgetting that reads the FAT itself as though it
 * were the root directory - which is a directory full of plausible-looking
 * entries that are not any.
 */
static u32 data_start;          /* sectors before the data area */

static u32 cluster_sector(u32 cluster)
{
    return data_start + (cluster - 2) * sectors_per_cluster;
}

static u32 cluster_bytes(void)
{
    return sectors_per_cluster * bytes_per_sector;
}

/*
 * Compare a directory entry's 8.3 name with what we are looking for.
 *
 * Case-insensitively, and padded with spaces to eleven characters, which is
 * what a short name is: eight of name, then three of extension.  LEOIMG.NBH
 * arrives as "LEOIMG  NBH".
 */
static int name_matches(const u8 *entry, const char *want)
{
    u8 have[11];
    int i = 0;
    int saw_dot = 0;
    int k;

    /*
     * A short name is eleven bytes: eight of name, three of extension, both
     * space-padded.
     *
     * Two things this gets wrong if written naively.  The extension starts at
     * offset 8 whatever the name's length was - carrying on from where the
     * name stopped puts "LEOIMG.NBH" in as "LEOIMGNBH  ", which matches
     * nothing.  And the field starts as all spaces, so the gap between a short
     * name and its extension is the padding it is and not whatever was on the
     * stack.
     */
    memset(have, ' ', sizeof(have));
    for (const char *p = want; *p; p++) {
        if (*p == '.') {
            if (saw_dot || i > 8) {
                return 0;      /* two dots, or a name longer than eight */
            }
            saw_dot = 1;
            i = 8;
            continue;
        }
        if (i >= 11) {
            return 0;
        }
        have[i++] = (u8)*p;
    }
    if (!saw_dot) {
        i = 8;                  /* no extension given: it is empty */
    }

    for (k = 0; k < 11; k++) {
        u8 a = entry[DIR_NAME + k];
        u8 b = have[k];

        if (a >= 'a' && a <= 'z') {
            a = (u8)(a - 'a' + 'A');
        }
        if (b >= 'a' && b <= 'z') {
            b = (u8)(b - 'a' + 'A');
        }
        if (a != b) {
            return 0;
        }
    }
    return 1;
}

/* Read the BPB out of the volume's first sector and check it. */
static int read_bpb(const struct sd_card *card)
{
    bytes_per_sector = 0;
    sectors_per_cluster = 0;
    fat_sectors = 0;
    root_cluster = 0;
    data_start = 0;
    label[0] = '\0';
    fat_cached_valid = 0;

    if (vol_sector(card, 0, block)) {
        return -1;
    }
    bytes_per_sector = read_le16(&block[BPB_BYTES_SECTOR]);
    sectors_per_cluster = block[BPB_SECTORS_CLUSTER];
    fat_sectors = read_le16(&block[BPB_FAT_SECTORS_16]);
    if (!fat_sectors) {
        fat_sectors = (u32)block[BPB_FAT_SECTORS_32] |
                      ((u32)block[BPB_FAT_SECTORS_32 + 1] << 8) |
                      ((u32)block[BPB_FAT_SECTORS_32 + 2] << 16) |
                      ((u32)block[BPB_FAT_SECTORS_32 + 3] << 24);
    }
    root_cluster = read_le32(&block[BPB_ROOT_CLUSTER]);
    data_start = read_le16(&block[BPB_RESERVED]) +
                 (u32)block[BPB_FATS] * fat_sectors;

    /*
     * What makes this a FAT32 volume rather than something else.
     *
     * The FAT-size-16 field being zero is *not* a reason to refuse: a FAT32
     * volume has it zero by definition, which is how it is told apart from a
     * FAT16 one, and refusing it here refuses every FAT32 card there is.
     *
     * The volume label at 0x43 and the FS version at 0x29 would both say more
     * about who formatted it than about what it is, and cards formatted by a
     * phone rather than a PC do not always carry them.
     */
    if (block[0] != 0xeb && block[0] != 0xe9) {
        return -1;              /* no jump instruction: not a boot sector */
    }
    if (bytes_per_sector != SECTOR || !sectors_per_cluster ||
        !fat_sectors || root_cluster < 2) {
        return -1;
    }
    if (sectors_per_cluster & (sectors_per_cluster - 1)) {
        return -1;              /* not a power of two, so not a real one */
    }
    if (block[BPB_FATS] < 1) {
        return -1;
    }
    memcpy(label, &block[BPB_LABEL_OFF], 11);
    label[11] = '\0';
    for (int i = 10; i >= 0; i--) {
        if (label[i] == ' ') {
            label[i] = '\0';
        }
    }
    return 0;
}

/*
 * An MBR: four 16-byte entries at 0x1be.  The type at +4 says what the
 * partition is; the start sector is a 32-bit little-endian at +8.
 *
 * read_le32 and not a struct, for the reason this project keeps having to
 * learn: the partition entry's start sector is at 0x1c6, two bytes into a word,
 * and with translation off the default memory type is Device, which does not
 * do unaligned transfers.  A word load there is a data abort, not a read.
 */
static int try_mbr(const struct sd_card *card)
{
    static const u8 want[] = { MBR_FAT32_LBA, MBR_FAT32_CHS, MBR_FAT16_LBA,
                               MBR_FAT_LBA };

    if (sd_read_block(card, 0, block)) {
        return -1;
    }
    if (block[510] != 0x55 || block[511] != 0xaa) {
        return -1;
    }
    for (int i = 0; i < 4; i++) {
        const u8 *e = block + 0x1be + 16 * (u32)i;
        u8 type = e[4];
        u32 start, count;

        {
            u32 w;

            for (w = 0; w < sizeof(want); w++) {
                if (type == want[w]) {
                    break;
                }
            }
            if (w == sizeof(want)) {
                continue;       /* not a FAT partition */
            }
        }
        start = read_le32(e + 8);
        count = read_le32(e + 12);
        if (start && count) {
            part_lba = start;
            return read_bpb(card);
        }
    }
    return -1;
}

/*
 * GPT: a protective MBR at LBA 0, "EFI PART" at LBA 1, and the entries
 * starting at the LBA it names.  Each entry is 128 bytes: a type GUID, a
 * unique GUID, then the first and last LBA as 64-bit little-endian.
 *
 * The type GUID is compared whole rather than by a prefix, because the one
 * that matters - Microsoft basic data - is not the FAT32 GUID anybody
 * expects: a card formatted on Windows gets this one, and that is the card
 * somebody flashing a phone will have.
 */
static int gpt_usable(const struct sd_card *card, const u8 *h, u32 *first,
                      u32 *last)
{
    static const u8 basic_data[16] = {
        0xeb, 0xd3, 0x90, 0x02, 0x5c, 0xdf, 0x1e, 0x11,
        0xa8, 0x43, 0x00, 0xa0, 0xc9, 0x1e, 0xfb, 0x8b
    };
    static const u8 efi_system[16] = {
        0x28, 0x73, 0x2a, 0x11, 0x1d, 0xb2, 0x11, 0x44,
        0x82, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    u32 entries_lba = read_le32(h + 0x48);
    u32 entries = read_le16(h + 0x50);
    u32 entry_bytes = read_le16(h + 0x54);

    if (entries == 0 || entries > 128 || entry_bytes < 128 || entry_bytes > 4096) {
        return -1;
    }
    for (u32 i = 0; i < entries; i++) {
        const u8 *e;
        u32 entry_lba = entries_lba + (i * entry_bytes) / SECTOR;
        u32 off = (i * entry_bytes) % SECTOR;

        if (sd_read_block(card, entry_lba, block)) {
            return -1;
        }
        if (off + 128 > SECTOR) {
            /* an entry straddling a sector boundary; rare enough to refuse */
            return -1;
        }
        e = block + off;
        if (!memcmp(e, basic_data, 16) || !memcmp(e, efi_system, 16)) {
            u64 lo = 0;
            u64 hi = 0;

            for (int b = 7; b >= 0; b--) {
                lo = (lo << 8) | e[32 + (u32)b];
                hi = (hi << 8) | e[40 + (u32)b];
            }
            if (hi > lo && lo > 0x800) {
                *first = (u32)lo;
                *last = (u32)hi;
                return 0;
            }
        }
    }
    return -1;
}

static int try_gpt(const struct sd_card *card)
{
    u32 first, last;

    if (sd_read_block(card, 1, block)) {
        return -1;
    }
    if (memcmp(block, "EFI PART", 8)) {
        return -1;
    }
    if (gpt_usable(card, block, &first, &last)) {
        return -1;
    }
    part_lba = first;
    return read_bpb(card);
}

int fat_mount(const struct sd_card *card)
{
    if (!try_mbr(card)) {
        return 0;
    }
    return try_gpt(card);
}

/* Find `name` in the root directory. */
int fat_open_root(const struct sd_card *card, const char *name,
                  struct fat_file *f)
{
    u32 cluster = root_cluster;
    u32 guard = 0;

    if (!bytes_per_sector || !root_cluster) {
        return -1;
    }
    while (cluster >= 2 && cluster < 0x0ffffff0 && guard++ < 1u << 20) {
        u32 sec = cluster_sector(cluster);
        u32 n = sectors_per_cluster;

        for (u32 s = 0; s < n; s++) {
            const u8 *e;

            if (vol_sector(card, sec + s, block)) {
                return -1;
            }
            for (u32 o = 0; o < SECTOR; o += 32) {
                e = block + o;
                if (e[0] == 0x00) {
                    return -1;  /* end of the directory: not here */
                }
                if (e[0] == 0xe5 || (e[DIR_ATTR] & ATTR_LFN) == ATTR_LFN ||
                    (e[DIR_ATTR] & ATTR_VOLUME_ID)) {
                    continue;
                }
                if (!name_matches(e, name)) {
                    continue;
                }
                f->card = card;
                f->first_cluster = read_le16(e + DIR_CLUSTER_LO) |
                                   ((u32)read_le16(e + DIR_CLUSTER_HI) << 16);
                f->size = read_le32(e + DIR_SIZE);
                f->pos = 0;
                f->open = 1;
                return 0;
            }
        }
        cluster = fat_next(card, cluster);
    }
    return -1;
}

/*
 * Read from a file.
 *
 * The file is not held in memory - it is 287 MB and RAM is not - so each read
 * is translated to a cluster, then to a sector, and the sector is cached.  A
 * caller reading forwards gets one card read per sector and no more, which is
 * what makes streaming an image into the flash take minutes rather than days.
 */
static int read_sector(const struct fat_file *f, u32 sector, void *dst)
{
    static u32 cached = 0xffffffff;
    static int valid;
    static u8 buf[SECTOR] __attribute__((aligned(4)));

    if (valid && cached == sector) {
        memcpy(dst, buf, SECTOR);
        return 0;
    }
    if (vol_sector(f->card, sector, buf)) {
        return -1;
    }
    cached = sector;
    valid = 1;
    memcpy(dst, buf, SECTOR);
    return 0;
}

int fat_read(const struct fat_file *f, u32 off, void *dst, u32 len)
{
    u8 *out = dst;
    u32 cluster_bytes_ = cluster_bytes();

    if (!f->open) {
        return -1;
    }
    while (len) {
        u32 within = off % cluster_bytes_;
        u32 sector_in = within / SECTOR;
        u32 byte_in = within % SECTOR;
        u32 n = SECTOR - byte_in;
        u32 cluster_index = off / cluster_bytes_;
        u32 sector;
        u32 cluster = f->first_cluster;

        if (n > len) {
            n = len;
        }
        if (off >= f->size) {
            return -1;
        }

        /* walk to the cluster: cheap, because the read is forwards */
        for (u32 i = 0; i < cluster_index; i++) {
            u32 next = fat_next(f->card, cluster);

            if (next < 2 || next >= 0x0ffffff0) {
                return -1;      /* the chain ended early */
            }
            cluster = next;
        }
        sector = cluster_sector(cluster) + sector_in;

        if (read_sector(f, sector, block)) {
            return -1;
        }
        memcpy(out, block + byte_in, n);
        out += n;
        off += n;
        len -= n;
    }
    return 0;
}

int fat_seek(struct fat_file *f, u32 off)
{
    if (off > f->size) {
        return -1;
    }
    f->pos = off;
    return 0;
}

int fat_read_next(struct fat_file *f, void *dst, u32 len)
{
    if (!f->open || f->pos >= f->size) {
        return 0;               /* nothing left: not an error */
    }
    if (f->pos + len > f->size) {
        len = f->size - f->pos;
    }
    if (fat_read(f, f->pos, dst, len)) {
        return -1;
    }
    f->pos += len;
    return (int)len;
}
