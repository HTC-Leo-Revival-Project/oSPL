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
#include <sys/mman.h>

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
#define LOAD_SIZE (1u << 20)

static u8 *mapped;

int main(int argc, char **argv)
{
    int failures = 0;
    int skipped = 0;

    for (int a = 1; a < argc; a++) {
        struct nbh nb;
        const char *why = NULL;
        FILE *f;
        long size;
        int r;

        if (!mapped) {
            mapped = mmap((void *)(uintptr_t)LOAD_ADDR, LOAD_SIZE,
                          PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
            if (mapped == MAP_FAILED) {
                printf("nbh-test: cannot map 0x%x\n", LOAD_ADDR);
                return 1;
            }
            printf("nbh-test: file mapped at 0x%x\n", LOAD_ADDR);
        }
        memset(mapped, 0, LOAD_SIZE);

        f = fopen(argv[a], "rb");
        if (!f) {
            printf("FAIL %s: cannot open it\n", argv[a]);
            failures++;

            continue;
        }
        size = (long)fread(mapped, 1, LOAD_SIZE, f);
        fclose(f);

        printf("\n%s (%ld bytes)\n", argv[a], size);
        r = nbh_open(LOAD_ADDR, (u32)size, &nb, &why);
        if (r == NBH_E_SIGNATURE) {
            /*
             * Not a failure: this is the other container.  The RUU's NBH also
             * starts R000FF but carries no signature and is framed quite
             * differently - the block list does not start where this one's
             * does.  Refusing it is the correct behaviour and saying so is
             * more use than reporting it as broken; what it would take to
             * parse it is in reverse/NOTES.md.
             */
            printf("  skipped: a different container - %s\n", why);
            skipped++;
            continue;
        }
        if (r) {
            printf("  FAIL: %s (%d)\n", why ? why : "no reason given", r);
            failures++;
            continue;
        }

        printf("  blocks      %u, %u bytes joined\n", nb.block_count,
               nb.image_len);
        printf("  version     \"%s\" build \"%s\"\n", nb.version, nb.build);
        printf("  target      0x%x\n", nb.target);
        printf("  OS area     +0x%x, %u bytes, %u sectors (%u in flash)\n",
               nb.os_off, nb.os_bytes, nb.os_sectors, nb.os_flash_bytes);
        printf("  MBR part    %d: %u sectors from %u\n", nb.mbr_part,
               nb.part_sectors, nb.start_sector);
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
            u32 off = NBH_OS_OFF;
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
            CHECK("the area fits in the block it is written to",
                  nb.os_flash_bytes <= NAND_BLOCK_SIZE);
            CHECK("the image starts past the MBR and its tag",
                  nb.start_sector >= 2);
            CHECK("the image fits inside the area",
                  nb.start_sector + nb.part_sectors <= nb.os_sectors);
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
        printf("%d file(s) are the other container and were not checked\n",
               skipped);
    }
    return failures ? 1 : 0;
}
