#!/bin/sh
#
# ruu-extract.sh - get the NBH out of an HTC RUU.
#
# usage: tools/ruu-extract.sh <RUU.exe> [outdir]
#
# An RUU is an InstallShield cabinet with the phone's image inside it.  This
# only gets the file out of the wrapper; nothing here has any opinion about
# what is in it, and nothing here writes to a flash.
#
# That matters, and it is the whole reason this is a separate step: on a
# phone the NBH is already sitting on the card and the bootloader reads it
# there.  The only reason anything has to unpack a cabinet here is that the
# emulator has no SD card yet, so the file has to be made first.
#
# The result lands next to the other images as RUU_signed.nbh.
set -e

here=$(cd "$(dirname "$0")" && pwd)
spl=$(dirname "$here")
emu=${EMU:-$(dirname "$spl")/qemu-hd2}

[ $# -ge 1 ] || { sed -n '3,17p' "$0" | sed 's/^# \{0,1\}//'; exit 1; }

ruu=$1
out=${2:-$emu}
[ -f "$ruu" ] || { echo "ruu-extract: $ruu does not exist" >&2; exit 1; }

sevenz=${SEVENZ:-}
if [ -z "$sevenz" ]; then
    for c in 7z 7za 7zr; do
        if command -v "$c" >/dev/null 2>&1; then sevenz=$c; break; fi
    done
fi
[ -n "$sevenz" ] || { echo "ruu-extract: no 7z found" >&2; exit 1; }

mkdir -p "$out"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

say() { printf 'ruu-extract: %s\n' "$*"; }
say "source   $ruu ($(stat -c%s "$ruu") bytes)"
say "unpacking with $sevenz"
"$sevenz" x -o"$tmp" "$ruu" -y >/dev/null

nbh=$(ls "$tmp"/*.nbh 2>/dev/null | head -1)
[ -n "$nbh" ] || { echo "ruu-extract: no NBH inside it" >&2; exit 1; }

say "found    $(basename "$nbh") ($(stat -c%s "$nbh") bytes)"
cp "$nbh" "$out/RUU_signed.nbh"
say "written  $out/RUU_signed.nbh"

# and say what it holds, because the point of getting it out is to look at it
say "sections:"
"$spl/build/nbh-test" "$out/RUU_signed.nbh" 2>/dev/null | sed 's/^/  /' || true
