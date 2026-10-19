#!/bin/sh
#
# mkcard.sh - a microSD card with an NBH on its root.
#
# usage: tools/mkcard.sh <card.img> <LEOIMG.nbh> [--size-mb 1024] [--table mbr|gpt]
#
# A FAT32 card with the image on its root, which is what the phone's own
# bootloader looks for: a card in the microSD slot, LEOIMG.nbh in the root of
# its first partition.
#
# --table picks the partition table, because cards turn up with both: an MBR
# from anything PC-era and a GPT from anything made recently, and the
# bootloader reads either.  sgdisk writes the GPT and sfdisk the MBR, mkfs.vfat
# the filesystem and mcopy the file; nothing here is hand-rolled, because a
# filesystem written by hand is a filesystem with hand-written bugs.
#
# This is the host half and nothing more.  It formats a card and puts a file on
# it.  The bootloader reads that file off the card and flashes it.
set -e

say() { printf 'mkcard: %s\n' "$*"; }
die() { printf 'mkcard: %s\n' "$*" >&2; exit 1; }

[ $# -ge 2 ] || { sed -n '3,19p' "$0" | sed 's/^# \{0,1\}//'; exit 1; }

card=$1; nbh=$2; shift 2
size_mb=1024
table=mbr

while [ $# -gt 0 ]; do
    case $1 in
    --size-mb) size_mb=$2; shift 2 ;;
    --table)   table=$2;   shift 2 ;;
    *) die "unknown option $1" ;;
    esac
done

[ -f "$nbh" ] || die "$nbh does not exist"
need=$(stat -c%s "$nbh")

for t in sgdisk sfdisk mkfs.vfat mcopy; do
    command -v "$t" >/dev/null 2>&1 || die "$t is not installed"
done

say "card    $card ($size_mb MB, $table)"
rm -f "$card"
dd if=/dev/zero of="$card" bs=1M count="$size_mb" status=none

# One MiB in, as Windows does it, so the first partition is 2k-aligned.
start_sector=2048

total_sectors=$((size_mb * 2048))
part_sectors=$((total_sectors - start_sector))

say "table   $table ($start_sector .. $total_sectors)"
case $table in
mbr)
    # sfdisk takes its geometry from the size and is happy to be told nothing
    # else: one partition, type c (FAT32 LBA), filling the card.
    sfdisk --quiet "$card" <<EOF
label: dos
label-id: 0x74657374
unit: sectors

start=$start_sector, size=$part_sectors, type=c, bootable
EOF
    ;;
gpt)
    sgdisk --clear \
           --new=1:$start_sector:$total_sector \
           --typecode=1:0c \
           --change-name=1:LEOIMG \
           "$card" >/dev/null
    ;;
*)
    die "--table must be mbr or gpt, not $table"
    ;;
esac

# mkfs.vfat needs a real block device, so the image goes round a loop for the
# formatting and the file is copied afterwards straight into the image with
# mtools, which can be pointed at a partition inside a file with @@offset.
loop=$(losetup --find --show --partscan "$card")
case $loop in
*/loop[0-9]*) partdev=${loop}p1 ;;      # /dev/loopN -> /dev/loopNp1
*)            partdev=${loop}1  ;;      # /dev/loopN1 style naming
esac
say "loop    $loop -> $partdev"
cleanup() { losetup -d "$loop" 2>/dev/null || true; }
trap cleanup EXIT

say "format  FAT32, volume name LEOIMG"
mkfs.vfat -F 32 -n LEOIMG "$partdev" >/dev/null

losetup -d "$loop"
trap - EXIT

# The partition starts at sector 2048 on both tables; mtools takes the offset
# in bytes.
offset=$((start_sector * 512))
say "copy    $(basename "$nbh") to the root"
mcopy -i "$card@@$offset" "$nbh" ::/LEOIMG.NBH

say "done    $(du -h "$card" | cut -f1) of card, $need bytes of image"
