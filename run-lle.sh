#!/bin/sh
# Boot the SPL in the HD2 emulator and keep the log.
#
# This always runs in baseband/DSP LLE mode, so the real AMSS comes up on the
# modem core exactly as it would on the phone, rather than the stub the
# machine stands in for.  USB=1 additionally hands the gadget to this machine's
# own USB stack over vhci_hcd instead of the adb-over-TCP socket, which is
# what makes the phone visible to lsusb and lets USB be watched from here.
# That needs root and the module loaded, so it goes back through sudo - which
# is why this is a script and not a line in the Makefile.
#
#   usage: run-lle.sh [sdl|headless]        the display (default sdl)
#          USB=1 run-lle.sh sdl             also USB over vhci_hcd
#          NAND=nand-clk.img run-lle.sh     another NAND image
#          OUT=/tmp/x run-lle.sh            put the logs somewhere else
#
# The SPL's console is UART1.  The log is written as the bytes arrive rather
# than by QEMU, which buffers its file chardev and loses the tail on a kill -
# most of the output of a bootloader that says seven lines.
set -e

SPL_DIR=$(cd "$(dirname "$0")" && pwd)
EMU=${EMU_DIR:-../qemu-hd2}
NAND=${NAND:-nand.img}
#
# SD=<card.img> puts a card in the microSD slot, which is where the
# bootloader looks for LEOIMG.nbh.  It is a FAT32 card with the file on its
# root; tools/mkcard.sh makes one.
SD=${SD:-}
MODE=${1:-sdl}
OUT=${OUT:-$SPL_DIR/build}

say() { echo "run-lle: $*"; }

[ -f "$SPL_DIR/build/spl.bin" ] || {
	say "no build/spl.bin - run make first"; exit 1; }

case $MODE in
sdl|headless) ;;
*) say "usage: run-lle.sh [sdl|headless]"; exit 1 ;;
esac

# USB over the host's vhci_hcd has to be asked for as root: the module's
# attach attribute is root-only and the socket fd has to still be open in the
# process that writes it, so it cannot be done from the side.
#
# sudo resets the environment and drops the argument, so everything this script
# reads is named again on the way in, and SPL_ROOTED says not to come back a
# second time - without which this execs itself through sudo for ever.
if [ -n "${USB:-}" ] && [ -z "${SPL_ROOTED:-}" ]; then
	say "loading vhci_hcd and coming back through sudo"
	sudo modprobe vhci_hcd 2>/dev/null || true
	exec sudo /usr/bin/sh -c "
		SPL_ROOTED=1 USB=1 OUT='$OUT' NAND='$NAND' EMU_DIR='$EMU' \\
		AMSS='${AMSS:-}' TRACE='${TRACE:-}' \\
		exec '$SPL_DIR/run-lle.sh' '$MODE'
	"
fi

say "SPL       $SPL_DIR/build/spl.bin"
say "emulator  $EMU"
say "NAND      $NAND"
[ -z "$SD" ] || say "SD card   $SD"
#
# Opened snapshot=on, so nothing a session does to the flash outlives it.  A
# bootloader with erase and nbhflash in it is one keystroke from destroying the
# image it boots from, and a test run has no business leaving the NAND in a
# state the next run has to notice.  tools/nbh-flash.py copies the image
# instead, because it wants to keep the result.

say "mode      baseband/DSP LLE${USB:+, USB over vhci_hcd}"
say "display   $MODE"
[ -n "${KEYS:-}" ] && say "keys      $KEYS (held down at reset)"
[ -n "${CMDS:-}" ] && say "commands  $(printf '%s ' "$CMDS")"

mkdir -p "$OUT"
UART="$OUT/uart1.log"
RAMCON="$OUT/ramcon.log"
SOCK=$(mktemp -u "$OUT/uart1.XXXXXX")
rm -f "$UART" "$RAMCON" "$SOCK" "$OUT/qemu.out"

if [ "$MODE" = sdl ]; then
	SCREEN="-display sdl"
else
	SCREEN="-display none"
fi

M="htcleo,baseband=lle,baseband-firmware=${AMSS:-amss.elf},baseband-trace=${TRACE:-off}"
[ -n "${USB:-}" ] && M="$M,usb-host=lle"
# KEYS=voldown holds a key down at reset, which is how the bootloader is asked
# to become the bootloader rather than start the OS.
[ -n "${KEYS:-}" ] && M="$M,keys=$KEYS"

M="$M,spl=$SPL_DIR/build/spl.bin"

# The UART1 log goes through a socket, because a file: chardev is buffered and
# drops the last of it when QEMU is killed.  wait=on on the server is the other
# half of it: QEMU binds the socket and then blocks for the client before the
# machine is built, so not one byte the guest writes can arrive before anyone
# is listening for it.
# One reader per unix socket, so the thing that logs the console is also the
# thing that can type at it.  CMDS="info" passes bootloader commands to type.
# CMDS is a list of commands to type at the bootloader's prompt, one per line.
# It goes to console.py through the environment rather than as arguments: a
# command with a space in it does not survive a shell variable and word-split
# back apart again.
cd "$EMU/images"
python3 "$SPL_DIR/tools/console.py" "$SOCK" "$UART" &
LOGGER=$!

say "starting QEMU"
../src/qemu/build/qemu-system-arm \
	-L ../src/qemu/pc-bios -L ../src/qemu/build/pc-bios \
	-M "$M" \
	-drive if=mtd,format=raw,file="$NAND",snapshot=on \
	${SD:+-drive if=sd,file="$SD",format=raw} \
	$SCREEN \
	-serial "unix:$SOCK,server=on,wait=on" \
	-serial file:"$RAMCON" \
	-audiodev none,id=snd0 -global msm-modem.audiodev=snd0 \
	> "$OUT/qemu.out" 2>&1 &
QEMU=$!

say "pid $QEMU; the console is $UART"
say "Ctrl-C here stops it"

# Whatever happened - a clean exit or a Ctrl-C - show what was said.
finish() {
	kill $QEMU 2>/dev/null
	wait $QEMU 2>/dev/null
	kill $LOGGER 2>/dev/null
	wait $LOGGER 2>/dev/null
	say "the console said:"
	if [ -s "$UART" ]; then
		tr '\r' '\n' < "$UART" | grep -a . || true
	else
		say "  (nothing)"
	fi
}
trap 'finish; exit 0' INT TERM

wait $QEMU || true
finish