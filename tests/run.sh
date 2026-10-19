#!/bin/sh
#
# run.sh - build and run the tests that do not need the machine.
#
# usage: tests/run.sh [file.nbh ...]
#
# With no files it looks for the ones that are usually lying around:
# LEOIMG.nbh and cLK.nbh, in the emulator's images directory and build
# sources.  They are real flashing files, so they are what the parser is
# checked against - the check is worth exactly as much as the files are real.
#
# This builds the parts of the code that are pure logic against a host stand-in
# for the machine (tests/host/).  It is not an emulator: nothing that touches
# hardware runs here, and nothing here pretends otherwise.  For that, see
# run-lle.sh.
set -e

here=$(cd "$(dirname "$0")" && pwd)
spl=$(dirname "$here")
emu=${EMU:-$(dirname "$spl")/qemu-hd2}

CC=${CC:-clang}
out=$spl/build/nbh-test

say() { printf 'run.sh: %s\n' "$*"; }

say "building $out against tests/host/"
mkdir -p "$spl/build"
# The pointer casts in the driver are right for the target and wrong here: on
# a 64-bit host a u32 address is not a pointer.  They are how a bootloader has
# to write a physical address, so they are left alone and the warning about
# them is only turned off for this build.
$CC -std=gnu11 -O1 -g -Wall -Wextra \
    -Wno-int-to-pointer-cast -Wno-int-to-void-pointer-cast \
    -I "$here/host" -I "$here/host/drivers" -I "$here/host/drivers/uart" \
    -I "$here/host/drivers/nand" \
    -I "$spl" -I "$spl/spl" -I "$spl/drivers/nbh" \
    "$here/nbh-test.c" "$here/stub.c" \
    "$spl/drivers/nbh/nbh.c" "$spl/spl/crc32.c" \
    -o "$out"

# The stock ROM's NBH is the interesting one: 287 MB with eight sections,
# against cCLK's single-section 128 KB file.  tools/ruu-extract.sh is what
# gets it out of the RUU, and it is a host step - nothing here writes to a
# flash.
#
# LEOIMG.nbh is the file this parses.  RUU_signed.nbh is passed too when it is
# lying about: it is the other container - the same magic, no signature, a
# different block framing - and the parser is expected to refuse it, which the
# test says is a skip rather than a failure.
if [ $# -gt 0 ]; then
    set -- "$@"
else
    set --
    for f in "$emu/LEOIMG.nbh" "$emu/images/LEOIMG.nbh" \
             "$emu/images/RUU_signed.nbh" "$emu/build/sources/RUU_signed.nbh" \
             "$emu/build/sources/wm-3.14.407.2/RUU_signed.nbh"
    do
        [ -f "$f" ] && set -- "$@" "$f"
    done
fi
files=$#

if [ $# -eq 0 ]; then
    say "no NBH files found; pass one as an argument"
    say "looked in $emu and $emu/images"
    exit 0
fi

if [ -z "$files" ]; then
    say "no NBH files found; pass one as an argument"
    say "looked in $emu, $emu/images and $emu/build/sources"
    exit 0
fi

say "checking:$files"
exec "$out" "$@"
