#!/bin/sh
#
# mkcard.sh - a microSD card with an NBH on its root.
#
# usage: tools/mkcard.sh <card.img> <LEOIMG.nbh> [size]
#
# A FAT32 card, which is what the phone's own bootloader looks for, with the
# image on its root.  Nothing here has an opinion about what is in the file:
# the bootloader reads it off the card and flashes it, the way it does on a
# phone.
#
#   size   the card's size in MB; 1024 by default
set -e
card=$1; nbh=$2; mb=${3:-1024}
tools=$(command -v mkfs.vfat || echo /sbin/mkfs.vfat)
mcopy=$(command -v mcopy || echo /usr/bin/mcopy)

say() { printf 'mkcard: %s\n' "$*"; }

[ -f "$nbh" ] || { echo "mkcard: $nbh does not exist" >&2; exit 1; }
need=$(stat -c%s "$nbh")

say "card    $card ($mb MB)"
dd if=/dev/zero of="$card" bs=1M count="$mb" status=none
say "format  FAT32, volume name LEOIMG"
"$tools" -F 32 -n LEOIMG "$card" >/dev/null

if [ -x "$mcopy" ] || command -v mcopy >/dev/null 2>&1; then
    say "copy    $(basename "$nbh") to the root"
    mcopy -i "$card" "$nbh" ::
else
    say "no mtools; the card is formatted but the file is not on it"
    say "install mtools to have the bootloader find it"
    exit 1
fi
say "done    $(du -h "$card" | cut -f1) used, $need bytes of image"
