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

## The NBH, and where this tree is not like the original

The phone flashes a `LEOIMG.nbh`, and the strings for it are in the image at
`0x72fa4` and after: `LEOIMG.nbh`, `LEOIMG.nbh - OK`, `LEOIMG.nbh - FAIL`,
alongside `GetExtRomData+()`, `Ext ROM Start Addr=0x%x, length...` and
`Storage Start Addr=0x%x`.  So the real bootloader routes the image header to
find the OS partition and checksums what comes out of it.

Ours does not: `autoboot()` reads the WinCE MBR in the OS area directly and
finds the image from that.  It works, and it is the same answer, but it is a
deviation and it is the biggest one left.

What has not been worked out yet is the header's own layout.  Nothing in the
image holds a pointer to those strings - they are reached by some other route
- and the code that reads them is past `0x6d000`, which is where the Ghidra
dump stops.  Reading the image rather than the dump got us the command table;
the header needs the same treatment and it has not had it yet.

## What still has to be worked out

- Whether `flags` is "hidden" or "locked" - `password` and the `Command is
  Locked!` string suggest a real lock, and this tree treats a zero as hidden.
- The NBH header layout, as above.
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