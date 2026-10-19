#!/bin/sh
# Boot the SPL in the HD2 emulator and keep the log.
#
# This always runs in baseband/DSP LLE mode, so the real AMSS comes up on the
# modem core exactly as it would on the phone, rather than the stub the
# machine stands in for.  USB=1 additionally hands the gadget to this machine's
# own USB stack over vhci_hcd instead of the adb-over-TCP socket, which is
# what makes the phone visible to lsusb and lets USB be watched from here.  That
# needs root and the module loaded, so it goes back through sudo - which is why
# this is a script and not a line in the Makefile.
#
#   usage: run-lle.sh [sdl|headless]        the display (default sdl)
#          USB=1 run-lle.sh                 also USB over vhci_hcd
#          NAND=nand-clk.img run-lle.sh     another NAND image
#
# The SPL's console is UART1 and the log is written as it arrives, so what is
# on disk is the whole conversation rather than the last buffer QEMU flushed.
set -e

SPL_DIR=$(cd "$(dirname "$0")" && pwd)
EMU=${EMU_DIR:-../qemu-hd2}
NAND=${NAND:-nand.img}
DISPLAY_MODE=${1:-sdl}
OUT=${OUT:-$SPL_DIR/build}

say() { echo "run-lle: $*"; }

[ -f "$SPL_DIR/build/spl.bin" ] || { say "no build/spl.bin - run make first"; exit 1; }

say "SPL       $SPL_DIR/build/spl.bin"
say "emulator  $EMU"
say "NAND      $NAND"
say "mode      baseband/DSP LLE${USB:+ , USB LLE${USB}}"
say "display   $DISPLAY_MODE"

mkdir -p "$OUT"
UART="$OUT/uart1.log"
RAMCON="$OUT/ramcon.log"
SOCK=$(mktemp -u "$OUT/uart1.XXXXXX")
rm -f "$UART" "$RAMCON" "$SOCK" "$OUT/qemu.out"

case $DISPLAY_MODE in
sdl)       DISPLAY="-display sdl" ;;
headless)  DISPLAY="-display none" ;;
*)         say "usage: run-lle.sh [sdl|headless]"; exit 1 ;;
esac

# USB over the host's vhci_hcd has to be asked for as root: the module's
# attach attribute is root-only and the socket fd has to still be open in the
# process that writes it, so it cannot be done from the side.
USB_LLE=
if [ -n "${USB:-}" ]; then
	say "loading vhci_hcd and coming back through sudo"
	sudo modprobe vhci_hcd 2>/dev/null || true
	exec sudo /usr/bin/sh -c "
		export OUT='$OUT' NAND='$NAND' DISPLAY_MODE='$DISPLAY_MODE'
		exec '$SPL_DIR/run-lle.sh'
	"
fi
USB_LLE=",usb-host=lle"

# The UART1 log goes through a socket, because a file: chardev is buffered and
# drops the last of it when QEMU is killed - which is most of the output of a
# bootloader that only says a few lines.  wait=on on the server is the other
# half of it: QEMU binds the socket and then blocks for the client before the
# machine is built, so not one byte the guest writes can arrive before anyone
# is listening for it.
cd "$EMU/images"
python3 "$SPL_DIR/uartlog.py" "$SOCK" "$UART" &
LOGGER=$!

say "starting QEMU"
../src/qemu/build/qemu-system-arm \
	-L ../src/qemu/pc-bios -L ../src/qemu/build/pc-bios \
	-M htcleo,baseband=lle,baseband-firmware=${AMSS:-amss.elf},baseband-trace=${TRACE:-off}$USB_LLE,spl=$SPL_DIR/build/spl.bin \
	-drive if=mtd,format=raw,file="$NAND" \
	$DISPLAY \
	-serial "unix:$SOCK,server=on,wait=on" \
	-serial file:"$RAMCON" \
	-audiodev none,id=snd0 -global msm-modem.audiodev=snd0 \
	> "$OUT/qemu.out" 2>&1 &
QEMU=$!

say "pid $QEMU; the console is $UART"
say "Ctrl-C here stops it"
trap 'say stopping; kill $QEMU 2>/dev/null; wait $QEMU 2>/dev/null; kill $LOGGER 2>/dev/null; exit 0' INT TERM

wait $QEMU || true
kill $LOGGER 2>/dev/null || true
say "QEMU exited"