# SPL

A second-stage bootloader for the QSD8250 in the HTC HD2, in C.

The phone ships one of these: HTC's own HSPL, which the boot ROM reads out
of NAND, and which in turn reads the OS image out of NAND and jumps to it.
This is that program again, written from scratch against what the hardware
actually does, small enough to read in one sitting.

It boots [cCLK](../clk), the Android build in `../clk`, out of the same NAND
image the emulator in [`../qemu-hd2`](../qemu-hd2) uses.

## What it does

On entry the core is in SVC mode at physical 0 with translation off and the
caches as the boot ROM left them. From there:

1. bring up the console, so there is something to say;
2. point the LCDC at a framebuffer, so there is something to show;
3. leave the board's identity in the globals page, where the OS reads it;
4. read the key matrix, because one of the three ways into the bootloader is
   somebody holding volume down as the phone comes up;
5. if that is the case, become the bootloader: draw its screen, print `BL>`,
   and read and run commands until somebody types `boot`;
6. otherwise identify the flash, find the OS image in the NAND "OS" area,
   load it to `0x11800000`, write the caches back, and jump there.

That is the same order the real HSPL works in, and it gets the same place: the
OS runs, and its menu comes up on the panel.

**The menu has all fifty-eight of the HSPL's commands** - the same names, so
anything you know about this phone's bootloader works here. The ones with
something real behind them do it: read and write memory, read and write the
flash, erase it, CRC a run of blocks, the panel, the touch screen, the
compass and the proximity sensor over I2C, the settings and the lock behind
them. The rest say plainly that this board has no WiFi dongle, no SD card and
no Bluetooth controller for them to talk to, rather than quietly doing
nothing.

What it does *not* do is everything the real HSPL does around that: the clocks
and the PMIC, the panel identify over MDDI, the EEPROM, the bad block table,
the data mover, the splash screen. Each of those belongs here too, and the
tree is arranged for them - see [Layout](#layout).

## Building

```
make            # build/spl.bin, and say how big it is
make run        # boot it in the emulator against ../qemu-hd2/images/nand.img
make dis        # disassemble it
make clean
```

The build uses the host's `clang` and `ld.lld`, cross-compiling for
bare-metal ARM. There is no `arm-none-eabi` toolchain in it and no libc:
the only things in the tree are the ones in it.

To see whether a change works, run it:

```
./run-lle.sh sdl            # baseband/DSP LLE, display up
./run-lle.sh headless       # the same, no display
USB=1 ./run-lle.sh sdl      # and USB over this machine's vhci_hcd
```

The real AMSS comes up on the modem core, so this is the configuration the
phone actually runs in. The SPL's own console is UART1, and the log is
written as the bytes arrive, so what is on disk is the whole conversation:

```
SPL: starting
SPL: flash maker=0xec device=0xbc, 4096 blocks of 64 pages
SPL: OS image, 62 sectors of 2048, at 0x11800000
SPL: clearing 126976 bytes at 0x11800000
SPL: loading
SPL: loaded
SPL: jumping to 0x11800000
```

## Layout

```
include/          board.h is the memory map, spl.h is types and helpers
drivers/uart      the console, on UART1DM
drivers/nand      the flash: identify, read a page, read a run skipping bad
                  blocks
drivers/display   the MDP display processor, the LCDC, and text on them
drivers/gpio      the GPIO banks
drivers/keypad    the 3x3 key matrix
drivers/i2c       the I2C bus
drivers/timer     delays, off the CSR's DGT
spl/start.c       the reset stub: a stack, and out of the way
spl/entry.c       .bss, and into C
spl/main.c        the boot flow, in the order it happens
spl/osimage.c     finding and loading the image in the NAND "OS" area
spl/boot.c        cleaning up and handing the core over
spl/bl.c          the bootloader's screen, its prompt and its commands
spl/flash.c       the commands that touch the NAND
spl/bus.c         the commands that talk to things on a bus
spl/misc.c        the panel, the power, the settings, and the rest of the menu
spl/format.c      the one printf, with the sinks as callbacks
linker/spl.ld     the flat image, entry at offset 0
reverse/          what was worked out of the phone's own HSPL, and the tools
tools/            the font generator, and the console session
run-lle.sh        boot it in the emulator, in baseband/DSP LLE mode
```

The drivers know about hardware and nothing else; the SPL main code knows
about boot order and nothing else.

## Notes on how it works

A few things here are not obvious, and are worth writing down.

**The MMU stays off the whole way through.** Every address in the tree is a
physical one. The boot ROM hands the core over with translation off, the
image it jumps to expects to be entered at a physical address, and nothing in
between needs a page table. One less thing to get wrong.

**That makes memory Device, and Device does not do unaligned transfers.** The
partition table in an MBR starts at offset `0x1BE`, so `StartSector` lands at
`0x1C6` - two bytes into a word. Reading it as a word is an unaligned `LDR`,
which the core takes as a fault rather than quietly assembling out of two
halves. `read_le32()` in `include/spl.h` is how those fields get read, and its
`volatile` is load-bearing: left to itself the compiler folds four byte reads
back into one aligned word load.

**`nand_read_page()` copies 512 bytes per codeword, not 528.** A page is four
codewords of 528 bytes, of which 512 are user data. In ECC mode with the
bad-block marker relocated (`CFG1` bit 16 clear) the controller has already
taken the marker out of the user area, so the front of the buffer is exactly
the page's data and the ECC, spare and marker bytes are all behind it.

**The jump is preceded by a cache clean, not just a cache disable.** The OS
image, the framebuffer and the globals all went through the D-cache, and
whatever is still dirty in it is invisible to whoever runs next. `boot.c`
walks the set/way space from `CTR` and `CLIDR` to write it all back.

**The link keeps out of `0x400`.** The boot ROM puts a coarse page table
there, for the window the board's globals are read through, and fills it in
before handing the core over. In the phone's own half-megabyte HSPL that is
zero padding; in a sixteen-kilobyte image it is live code, and a page table
written over it makes the image jump into the middle of a function.

## Test

`make run` boots it with the display up; the OS menu on the panel is the
thing to look at. Without a display:

```
make
../qemu-hd2/src/qemu/build/qemu-system-arm \
    -L ../qemu-hd2/src/qemu/pc-bios \
    -M htcleo,spl=$PWD/build/spl.bin \
    -drive if=mtd,format=raw,file=../qemu-hd2/images/nand.img \
    -display none -serial stdio
```

or `./run-lle.sh`, which is the same run with the modem core doing its real
job and a console that does not lose the last of itself.

A good boot is one `SPL: starting` and one `SPL: jumping`. More than one of
either means the core took an exception: with the boot ROM's `VBAR` still at
0, an exception lands on offset 0, which is `_start` again, and the whole
thing starts over.