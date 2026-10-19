#!/usr/bin/env python3
"""
disassemble the HSPL with its own strings resolved.

Ghidra's dump of this image names data references inside the code region and
loses the rest, but the strings are all there: the bootloader reaches them
through literal pools holding 0x95000000 + the file offset, and every one of
those constants is visible in a disassembly.  So: disassemble, and wherever a
constant points into the data area, print what it points at.

    ./annot.py 0x1f150 0x1f240     one range of the image
    ./annot.py --strings 'BL>'     where the strings are
    ./annot.py --use 0x95002eb0    who references this address
"""
import re
import struct
import sys

import capstone

import os

IMAGE = os.environ.get("HSPL",
    os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                 "..", "qemu-hd2", "images", "hspl.bin"))
BASE = 0x95000000
data = open(IMAGE, "rb").read()

# Every 32-bit word the image holds, so a constant can be recognised.
WORD_AT = {}
for off in range(0, len(data) - 3, 4):
    WORD_AT[off] = struct.unpack_from("<I", data, off)[0]


def text(off, maxlen=72):
    """the string at a file offset, if there is one"""
    if not 0 <= off < len(data):
        return None
    end = data.find(b"\0", off, off + maxlen)
    if end < 0:
        return None
    s = data[off:end]
    # the console strings are full of CR LF and the odd tab, so those count as
    # printable here and are shown escaped
    if len(s) < 2 or any(not (0x20 <= c <= 0x7E or c in (9, 10, 13))
                         for c in s):
        return None
    return (s.decode().replace("\r", "").replace("\n", "\\n")
            .replace("\t", " "))


def string_for(va):
    if not BASE <= va < BASE + len(data):
        return None
    return text(va - BASE)


def all_strings():
    off, out = 0, {}
    while off < len(data):
        if 0x20 <= data[off] <= 0x7E:
            s = text(off)
            if s and len(s) >= 3:
                out[off] = s
                off += len(s) + 1
                continue
        off += 1
    return out


ADD_PC = re.compile(r"\badd\s+r\d+,\s*pc,\s*#(-?(?:0x)?[0-9a-f]+)")


def computed_refs(insn, addr):
    """`add rN, pc, #imm` is how most of the HSPL's string addresses are made"""
    m = ADD_PC.search(insn)
    if not m:
        return []
    va = addr + 8 + int(m.group(1), 0)
    return [(None, va)]


def pool_refs(insn, addr):
    """
    the addresses a literal-pool load refers to.

    `ldr r0, [pc, #24]` reads the word at (pc + 8 + 24) & ~3, so the value
    it picks up is at a known offset and worth naming.
    """
    m = re.search(r"\bldr\s+r\d+,\s*\[pc,\s*#(-?(?:0x)?[0-9a-f]+)\]", insn)
    if not m:
        return []
    lit = ((addr + 8) + int(m.group(1), 0)) & ~3
    if not 0 <= lit < len(data):
        return []
    val = struct.unpack_from("<I", data, lit)[0]
    return [(lit, val)]


def show(lo, hi, mode="thumb"):
    md = capstone.Cs(capstone.CS_ARCH_ARM,
                      capstone.CS_MODE_THUMB if mode == "thumb"
                      else capstone.CS_MODE_ARM)
    md.detail = False
    blob = data[lo:hi]
    va0 = lo + BASE
    for insn in md.disasm(blob, va0):
        note = ""
        txt = insn.mnemonic + " " + insn.op_str
        refs = (pool_refs(txt, insn.address - BASE) +
                computed_refs(txt, insn.address - BASE))
        for lit, val in refs:
            s = string_for(val)
            if s:
                note = '  ; "%s"' % s
            elif BASE <= val < BASE + len(data):
                note = '  ; =0x%x (+0x%x)' % (val, val - BASE)
        print("%08x: %-9s %-34s%s" % (insn.address, insn.mnemonic,
                                      insn.op_str, note))


def use(va):
    """every place that loads this address's value from a literal pool"""
    for off in range(0, len(data) - 3, 4):
        if WORD_AT[off] == va:
            print("word at file 0x%06x (va 0x%08x)" % (off, off + BASE))


if __name__ == "__main__":
    a = sys.argv[1:]
    if not a:
        print(__doc__)
        sys.exit(1)
    if a[0] == "--strings":
        pat = a[1] if len(a) > 1 else ""
        for off, s in sorted(all_strings().items()):
            if pat in s:
                print("0x%06x  va 0x%08x  %r" % (off, off + BASE, s))
    elif a[0] == "--use":
        use(int(a[1], 16))
    elif a[0] == "--xrefs":
        lo, hi = int(a[1], 16), int(a[2], 16)
        mode = a[3] if len(a) > 3 else "arm"
        md = capstone.Cs(capstone.CS_ARCH_ARM,
                          capstone.CS_MODE_THUMB if mode == "thumb"
                          else capstone.CS_MODE_ARM)
        # ARM and Thumb are interleaved in this image, so a run of bytes the
        # decoder cannot make sense of is normal rather than the end
        md.skipdata = True
        for insn in md.disasm(data[lo:hi], lo + BASE):
            txt = insn.mnemonic + " " + insn.op_str
            for lit, val in (pool_refs(txt, insn.address - BASE) +
                             computed_refs(txt, insn.address - BASE)):
                st = string_for(val)
                if st:
                    print("%08x  0x%05x  %r" % (insn.address, val - BASE, st))
    else:
        lo = int(a[0], 16)
        hi = int(a[1], 16) if len(a) > 1 else lo + 0x80
        mode = a[2] if len(a) > 2 else "thumb"
        show(lo, hi, mode)