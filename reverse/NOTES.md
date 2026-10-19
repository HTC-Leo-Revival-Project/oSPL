# What the HSPL is, and what we have to build

Notes from reversing `images/hspl.bin` (525512 bytes, the phone's own HSPL
2.08). Tools that produced these are in this directory:

- `cmdtable.py` - recovers the command table out of the image
- `annot.py` - disassembles a range with the strings it refers to resolved
- `spl_decomp.c` - Ghidra's 1018-function dump, useful but blind to the data
  area (see below)

**The image runs at `0x95000000`.** It is loaded at physical 0, so every
address it uses for its own strings is `0x95000000` plus the file offset.
That is the whole trick for finding string references: every one of them is a
constant of that shape, visible either in a literal pool or computed as
`add rN, pc, #imm`.

Ghidra's dump names data references *inside the code window* (`DAT_9501f1a4`)
and silently drops everything below `0x1dd1c`, which is where most of the
strings live. So read the image, not the dump.

## The command table

`cmdtable.py` finds it: 58 records of twelve bytes at file offset `0x2fb0`,
each `{char *name, void (*handler)(char **args, int nargs), u8 flags}`.

| | |
|---|---|
| command | handler |
| `lrs` `lnbs` `wdatas` | `0x9501f434` `0x9501f4fc` `0x9501f5fc` |
| `btrouter` `btaddr` `fmrouter` | `0x9501f70c` `0x9501f734` `0x9501f720` |
| `checksum` `checkimage` `erase` | `0x950206a0` `0x9502071c` `0x95020894` |
| `mddir` `mddiw` | `0x9501f150` `0x9501f1ac` |
| `i2cr2` `i2cr` `i2cw` | `0x9501f224` `0x9501f29c` `0x9501f32c` |
| `display` `dispinit` | `0x9501f424` `0x95029634` |
| `tpd` `keypad` | `0x95062a74` `0x95021368` |
| `info` `set` `password` | `0x950208b8` `0x950208f0` `0x95020954` |
| `waddr` `wdata` | `0x95020970` `0x95020cec` |
| `blversion` `boot` | `0x95021218` `0x95033590` |
| `getdevinfo` `PowerDown` `ResetDevice` | `0x950210f0` `0x950210dc` `0x950210c0` |
| `ruustart` `progress` `rbmc` `task` | `0x95020e88` `0x95020f00` `0x950218fc` `0x95021820` |
| `ramdump` | `0x95021b88` |
| `micropchecksum` `proximitytest` `dcompasstest` | `0x95021690` `0x95021720` `0x950217b8` |
| `emapi*` (19 of them, the BCM4329 WiFi) | `0x9501f748` .. `0x95043560` |

The `flags` field is `0xff` for everything you can see in the menu and `0x00`
or small values for the ones you cannot - the `emapi*` manufacturing
commands, `btaddr`, `fmrouter`, and `proximitytest`/`dcompasstest` all read 0.
So it is a visibility or locking word, not an argument count.

## What a handler looks like

Every one of them has the same shape, and it is the whole of the menu's
parsing:

```c
void handler(char **args, int nargs);
```

`mddir` at `0x9501f150`, in full:

```asm
cmp   r1, #2              ; nargs == 2 ?
bne   usage
ldr   r0, [r0, #4]        ; args[1]
add   r1, sp, #0
bl    FUN_9501eb7c        ; strtoul(args[1], &value)
ldr   r0, [sp]
bl    FUN_95033dc4        ; do the thing
b     out
usage:
bl    FUN_9501e944        ; puts("******************************")
bl    FUN_9501e944        ; puts("CMD>mddir [Address]")
bl    FUN_9501e944        ; puts("******************************")
```

So `FUN_9501e944` is `puts`, `FUN_9501eb7c` is `strtoul` into an out
parameter, and `FUN_95033dc4` is the memory dump itself. The usage banner is
three puts calls and the arguments are parsed by hand per command - which is
why each handler carries its own copy of the check.

## The boot decision

Two functions, at `0x9501ef84` and (a few instructions later) `0x9501eecc`,
decide whether to become the bootloader or get out of the way:

```
FUN_95029794()                  ; some "auto BL" test
if (that)  -> puts("BL> RUU auto BL is TRUE")
else {
    if (FUN_9505f728())         ; the "enter BL" test - the key
        -> puts("BL> IsEnterBL is TRUE")
    else if (FUN_95029a94())    ; the NBH tag test
        -> puts("BL> RUU NBH Tag is TRUE"), a flag = 1
}
```

Three ways in, and they are the HSPL's whole personality:

- **RUU** - Qualcomm's Recovery/Update Utility has left a tag saying a
  programmed image is waiting, so stay in the bootloader.
- **the key** - `FUN_9505f728()` is the volume-down check. Held at reset,
  which is exactly what makes the rainbow screen and the `BL>` prompt come
  up.
- **the NBH tag** - `LEOIMG.nbh` says there is a flashed image to go to.

There is a `ClrRUUTagGoToOS` string at `0xdff4` (referenced from `0x29840`),
which is the other side of the first test: the tag is cleared when the OS is
about to be booted, so the next reset does not land back here.

## What the menu shows

The BL screen has a heading the image holds as `Serial`, `USB` and `Debug`
side by side at `0x2ebc`, and `dUmPFF` at `0x2eb4` next to them. Along with
the strings we know from booting it:

```
Security Unlocked
PB81100 SS-BC
SPL-2.08.HSPL 8G XA
MicroP-Wrong Platform(0x5)
```

and a rainbow, then `BL> ` (or `CMD>`) and the command list. `Cmd>` is at
`0x2ed5`.

## What has been done about it

All fifty-eight names are in the menu (`spl/misc.c`, `spl/flash.c`,
`spl/bus.c`, `spl/bl.c`), verified by comparing the two tables. The ones with
something behind them do it; the ones that need a WiFi dongle, an SD card or
a Bluetooth controller say that there is none.

`FUN_9505f728` turned out to be the volume-down key, read out of the 3x3
matrix at `0xa9100c00`: `drivers/keypad/keypad.c` scans it the way the model
wants to be scanned, and the emulator grew `-M keys=` because the HSPL reads
the matrix in the first moments after reset and a key event from outside
arrives far too late to matter.

## The NBH

The file is not an image; it is a signed wrapper around a header table and the
image area, and what the flash ends up holding is the area.  Read off the real
`LEOIMG.nbh`, not off the code - the code that reads it is past where the Ghidra
dump stops, and nothing in the image holds a pointer to the `LEOIMG` strings
either.  The format was already half-known in `qemu-hd2/tools/hd2_mknand.py`,
which unpacks it; this is the same thing in C.

    0x00  "R000FF"                six bytes of magic
    0x06  '\n'
    0x07  "XDA-Developers! "      sixteen bytes - the name is fifteen and
                                   the space is part of the field
    0x17  a block list, until one with a length of zero:
             u32  length           bytes of data
             u32  siglen           bytes of signature after it
             u8   flag             1: the data is signed
             u8   data[length]
             u8   sig[siglen]

Each block is 0x10000 bytes of data, so a 0x20400-byte image arrives as two
full blocks and one of 0x400.  Joined back together:

    0x000  the board and partition names, four bytes each, null-padded.
           The first two are the board ("HT"); the rest are the partitions
    0x020  where the image goes, as text: "PB8110000" -> 0x81100000
    0x040  0x00040000
    0x0c0  0x00020000
    0x140  how many bytes of OS area follow: 0x20200 = 64 x 0x808
    0x1c0  "11111111"
    0x1e0  the version as text: "CLK1.4.0.2"
    0x1f0  the build tag: "WWE"
    0x200  the OS area

The area is 64 sectors at a stride of 0x808: 2048 bytes of data and 8 bytes of
sector info after each.  The sector info is `u32 sector number`, then two bytes
that repeat the MBR's signature (`fd ff fb ff`).  Only the 2048 goes into the
flash - the info is what the file needed and the flash never had.  Sector 0 is
the WinCE MBR and sector 1 is `MSFLSH50`; `hd2_osarea.py` builds the same
arrangement directly, which is how `images/nand.img` gets its OS area.

Two bytes of every page are not the file's: the part keeps its bad-block marker
in the last codeword's user area at offset 464 and those bytes have to read
`0xFF` for the block to count as good.  Comparing a file against a flash that
has dutifully protected its own health therefore reports a difference that is
correct behaviour read as damage, so both sides are normalised before the
checksum is taken.

### `fmrouter` is not what it sounds like

It prints `--- Welcomm to HTC MFG FM Router ---`.  It is the manufacturing
bridge to the baseband over the message channel (`CheckRadioReady`, `AUD_HP_EN`,
`Turn on Radio`), not anything to do with routing partitions out of an image.
Our `fmrouter` lists the partitions the header names, which is what the name
suggests and is more use than a stub.

### The other container is the same container

`RUU_signed.nbh` from a stock ROM starts `R000FF` with no signature, and was
being reported as a different format.  It is not: it is the same block list,
one entry of 9 bytes plus the data plus 0x80 bytes of signature, and the
signature is optional.  HTCFlasher's own generator (`yang`, in
`HtcLeoPkg/ImageResources/Leo/yang_src`) writes `"XDA-Developers! "` at 0x07
and HTC's tool leaves sixteen zero bytes.  Both flash.  So the signature says
who wrote a file, not what it is, and refusing one over it was refusing a
working flash.

`yang`'s `struct HTCIMAGEHEADER` is the section table:

    char     device[32];          uint32_t sectiontypes[32];
    uint32_t sectionoffsets[32];  uint32_t sectionlengths[32];
    char     CID[32];  char version[16];  char language[16];

HTC's own files put the tables 0x20 bytes further on - types at 0x40, offsets
at 0xc0, lengths at 0x140 - because its device field is twice as wide.  Both
are read; the one that matters is whichever gives sections that add up.

### The sections, and where they go

    0x301  radio, 25 MB     boot area, FSBL, OSBL, AMSS, DSP1, and a MIBIB
                             partition table inside naming the NAND blocks
    0x380  512 B            "QCT_UMTS" - the modem's identity
    0x200  512 KB           code, `ea 0003fe` at the front: the loader that
                             runs before the OS
    0x600  768 KB           the splash
    0x400  259 MB           the OS: a WinCE area, MBR listing ULDR (0x20,
                             1598 sectors from 2), XIP (0x23, 2624 from 1600
                             - the kernel the bootloader jumps to), IMGFS
                             (0x25, 122176 from 4224) and storage (0x04,
                             101824 from 126400)
    0x920  2 KB             "CONF" - the radio's configuration
    0x610  54 KB            not identified
    0x611  70 KB            not identified

The OS section's pages each carry 8 bytes after the 2048: `u32 sector number`
then `fd ff fb ff`.  `nbgen.c` in the same directory writes the same thing -
`writetag(sectorNo, tag)` with `tag` being `0xFFFBFFFD` for sector 0,
`0xFFFBFFFF` after it and `0xFFFFFFFF` for padding - and the phone's flash
driver keeps it in the NAND spare area, four bytes to a codeword.  That is why
`nbh_flash()` had been dropping it and why dropping it is not survivable.

The OS area is 126400 sectors = 1975 blocks from 0x212, which is most of the
rest of the part.  A stock NBH is 4370 blocks of 0x10000 in the file; the last
is 0x5e44, because the image is not a whole number of 64 KB.

### What `fmrouter` is not

It prints `--- Welcomm to HTC MFG FM Router ---`.  It is the manufacturing
bridge to the baseband over the message channel, not anything to do with
routing partitions out of an image.

## What still has to be worked out

- Whether `flags` is "hidden" or "locked" - `password` and the `Command is
  Locked!` string suggest a real lock, and this tree treats a zero as hidden.
- Where each section other than 0x400 goes in the flash.  The radio's is
  worked out in `qemu-hd2/tools/hd2_radio.py` from the MIBIB table inside
  section 0x301 and has not been ported to C; 0x200, 0x600, 0x380, 0x920,
  0x610 and 0x611 have not been placed at all.  `nbh_flash_all()` lists them
  rather than guessing.
- What `fmrouter`'s message channel can actually do - whether the radio can be
  brought up from here at all.
- `dispinit` (`0x95029634`) and `tpd` (`0x95062a74`) - the panel and touch
  bring-up, which is where the MDDI traffic is.
- The `emapi*` group: nineteen commands driving a BCM4329 over SDIO, with
  firmware download, a packet engine and a MAC-address setter.
- `ramdump` (`0x95021b88`) - writing a crash dump to an SD card.
- The NBH handling behind `fmrouter`, `lnbs`, `lrs` and the `l c:\temp\lboot.bin`
  string: flashing the bootloader itself.
- Whether `flags` is "hidden" or "locked". `password` (`0x95020954`) and the
  `Need password!` / `Command is Locked!` strings suggest there is a real
  lock, not just a hidden menu.