/*
 * fat.h - FAT32 on the microSD card.
 */
#ifndef FAT_H
#define FAT_H

#include "spl.h"
#include "drivers/sd/sd.h"

struct fat_file {
    const struct sd_card *card;
    u32 first_cluster;   /* where the data starts              */
    u32 size;            /* how long it is, in bytes           */
    u32 pos;             /* how far in we have got             */
    int open;
};

/*
 * Read the card's partition table and its FAT.
 *
 * Both layouts are tried: an MBR, which is what every card ever written by a
 * PC has had, and GPT, which is what anything made recently has.  The order
 * matters - GPT puts its primary header at LBA 1, and an MBR at LBA 0 can be
 * there too on a GPT disk - so the MBR is checked first and only if it offers
 * nothing usable is GPT tried.
 *
 * 0 if there is a FAT32 volume on the card.
 */
int fat_mount(const struct sd_card *card);

/* One byte of the volume, by offset from its start.  0 on success. */
int fat_read(const struct fat_file *f, u32 off, void *dst, u32 len);

/*
 * Look for a file by name in the root directory, case-insensitively, which is
 * what FAT does.  Long names (LFN) are read and ignored: the short 8.3 name is
 * what is matched, and LEOIMG.NBH has one.
 */
int fat_open_root(const struct sd_card *card, const char *name,
                  struct fat_file *f);

/* A run of bytes, for a file being streamed rather than read in one go. */
int fat_seek(struct fat_file *f, u32 off);
int fat_read_next(struct fat_file *f, void *dst, u32 len);

/* What the volume says about itself, for the menu. */
const char *fat_volume_label(void);

#endif /* FAT_H */
