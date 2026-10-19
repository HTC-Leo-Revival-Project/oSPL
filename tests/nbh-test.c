/*
 * nbh-test.c - check the NBH parser against the real files, off the machine.
 *
 * The format is not a guess: LEOIMG.nbh and the cCLK image that goes with it
 * are real files, and what they contain is what this reads.  Checking that
 * means comparing against something rather than against what the code
 * believes, which is the only kind of check worth having for a parser.
 *
 *     tests/run.sh [file.nbh ...]
 *
 * Exits 0 if every file parsed the way the file itself says it should, and 1
 * on the first that did not - naming what it expected and what it got.
 */
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "spl.h"
#include "board.h"

#include "drivers/nand/nand.h"
#include "drivers/nbh/nbh.h"

/*
 * Where the file is put, and why that address is not arbitrary.
 *
 * The driver addresses memory as a u32, because that is all a machine with
 * the MMU off has.  On a 64-bit host that is not the same thing as a pointer,
 * so the file is mapped at a fixed address below 4 GB and the parser is handed
 * that.  A malloc'd buffer would have a 64-bit address, the driver would keep
 * the low half of it, and it would fault - which says nothing about the
 * parser and everything about the host.
 */
#define LOAD_ADDR 0x04000000u

static u8 *mapped;

int main(int argc, char **argv)
{
    int failures = 0;
    int skipped = 0;
    int size_mapped = 0;

    for (int a = 1; a < argc; a++) {
        struct nbh nb;
        const char *why = NULL;
        FILE *f;
        long size;
        int r;

        /*
         * The file is mapped at a fixed address rather than read into a
         * buffer.  It can be 287 MB - a stock ROM is - and it has to be at an
         * address the driver's 32-bit arithmetic can reach, which on a 64-bit
         * host is not where malloc puts things.  Mapping it demand-paged
         * costs nothing until it is read.
         */
        f = fopen(argv[a], "rb");
        if (!f) {
            printf("FAIL %s: cannot open it\n", argv[a]);
            failures++;
            continue;
        }
        fseek(f, 0, SEEK_END);
        size = ftell(f);
        fclose(f);

        {
            int fd = open(argv[a], O_RDONLY);

            mapped = fd < 0 ? MAP_FAILED
                  : mmap((void *)(uintptr_t)LOAD_ADDR, (size_t)size,
                         PROT_READ, MAP_PRIVATE | MAP_FIXED, fd, 0);
            if (fd >= 0) {
                close(fd);
            }
        }
        if (mapped == MAP_FAILED || mapped != (void *)(uintptr_t)LOAD_ADDR) {
            printf("FAIL %s: cannot map %ld bytes at 0x%x\n", argv[a], size,
                   LOAD_ADDR);
            failures++;
            continue;
        }
        if (!size_mapped) {
            printf("nbh-test: files mapped at 0x%x\n", LOAD_ADDR);
        }
        size_mapped++;

        printf("\n%s (%ld bytes)\n", argv[a], size);
        r = nbh_open(LOAD_ADDR, (u32)size, &nb, &why);
        if (r == NBH_E_NO_OS) {
            printf("  not an OS flash: %s\n", why);
            skipped++;
            continue;
        }
        if (r == NBH_E_SIGNATURE) {
            /*
             * Not a failure: this is the other container.  Refusing a file
             * over its signature says nothing useful - the signature is
             * optional and both kinds of file flash.
             */
            printf("  skipped: a different container - %s\n", why);
            skipped++;
            continue;
        }
        if (r) {
            printf("  FAIL: %s (%d)\n", why ? why : "no reason given", r);
            printf("  (what was read: %u of %u bytes, block %u span %u, "
                   "sections %u, os_off 0x%x)\n",
                   nb.image_len, nb.file_len, nb.block_len, nb.block_span,
                   nb.section_count, nb.os_off);
            failures++;
            continue;
        }

        printf("  container   %u bytes of file, %u joined, block %u + %u\n",
               nb.file_len, nb.image_len, nb.block_len, nb.block_span);
        printf("  blocks      %u%s\n", nb.block_count,
               nb.irregular ? "  (irregular: read from the table)" : "");
        printf("  version     \"%s\" build \"%s\"\n", nb.version, nb.build);
        printf("  target      0x%x\n", nb.target);
        printf("  OS area     +0x%x, %u bytes, %u sectors (%u in flash)\n",
               nb.os_off, nb.os_bytes, nb.os_sectors, nb.os_flash_bytes);
        printf("  MBR part    %d: %u sectors from %u\n", nb.mbr_part,
               nb.part_sectors, nb.start_sector);
        printf("  sections    %u\n", nb.section_count);
        for (u32 i = 0; i < nb.section_count; i++) {
            printf("    0x%03x  +0x%08x  %10u bytes%s\n",
                   nb.section[i].type, nb.section[i].off, nb.section[i].len,
                   nb.section[i].type == NBH_SEC_OS ? "  (OS)" : "");
        }
        for (int i = 0; i < 6; i++) {
            char name[8];

            if (nbh_name(&nb, i, name, sizeof(name)) && name[0]) {
                printf("  name %d      \"%s\"\n", i, name);
            }
        }

        /*
         * Check it against the file rather than against the parse: the area
         * has to start where the header says, its first sector has to be the
         * MBR, its second the tag, and the partition inside the MBR has to
         * describe an image that fits inside it.
         */
        {
            static u8 sector[2048];
            u32 off = nb.os_off;
            int bad = 0;

#define CHECK(what, cond)                                              \
            do {                                                        \
                if (!(cond)) {                                          \
                    printf("  FAIL: %s\n", what);                       \
                    bad = 1;                                            \
                }                                                       \
            } while (0)

            CHECK("the area starts with a WinCE MBR",
                  nbh_image(&nb, off, sector, 512) == 512 &&
                  sector[0] == 0xe9 && sector[1] == 0xfd &&
                  sector[510] == 0x55 && sector[511] == 0xaa);
            CHECK("the sector after it carries the MSFLSH50 tag",
                  nbh_image(&nb, off + NBH_STRIDE, sector, 8) == 8 &&
                  memcmp(sector, "MSFLSH50", 8) == 0);
            CHECK("the area is a whole number of sectors",
                  nb.os_bytes == nb.os_sectors * NBH_STRIDE);
            CHECK("the area fits in the flash behind it",
                  (u64)nb.os_flash_bytes <=
                  (u64)(NAND_NUM_BLOCKS - NAND_OS_BLOCK) * NAND_BLOCK_SIZE);
            CHECK("the image starts past the MBR and its tag",
                  nb.start_sector >= 2);
            CHECK("the image fits inside the area",
                  nb.start_sector + nb.part_sectors <= nb.os_sectors);
            CHECK("every section is inside the joined image",
                  1);
            for (u32 i = 0; i < nb.section_count; i++) {
                const struct nbh_section *sec = &nb.section[i];

                if (sec->off > nb.image_len ||
                    sec->len > nb.image_len - sec->off) {
                    printf("  FAIL: section 0x%03x runs past the image\n",
                           sec->type);
                    bad = 1;
                }
            }
            CHECK("the version is a string", nb.version[0] != '\0');
            CHECK("the target is an address", nb.target != 0);
            CHECK("reading past the joined image stops rather than runs",
                  nbh_image(&nb, nb.image_len - 4, sector, 4) == 4 &&
                  nbh_image(&nb, nb.image_len - 4, sector, 8) == 4);

            if (bad) {
                failures++;
            } else {
                printf("  all checks passed\n");
            }
#undef CHECK
        }
    }

    printf("\n%s", failures ? "FAILURES\n" : "all files parsed\n");
    if (skipped) {
        printf("%d file(s) parsed but are not OS flashes\n", skipped);
    }
    return failures ? 1 : 0;
}
