#!/usr/bin/env python3
"""
pull the HSPL's command table out of the image.

The bootloader calls its own strings by the virtual address it runs at:
0x95000000 plus the file offset the string sits at.  So the command table is
whatever array of {pointer-to-a-name, handler, flags} the image holds, and it
can be found by looking for pointers of that shape pointing at strings.

    ./cmdtable.py [hspl.bin]
"""
import os
import struct
import sys

IMAGE = sys.argv[1] if len(sys.argv) > 1 else os.environ.get(
    "HSPL", os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                         "..", "qemu-hd2", "images", "hspl.bin"))
BASE = 0x95000000        # the HSPL runs here; its file offsets are the low bits
END = BASE + 0x100000

data = open(IMAGE, "rb").read()


def text(off, maxlen=64):
    """the NUL-terminated string at a file offset, if it looks like one"""
    if not 0 <= off < len(data):
        return None
    end = data.find(b"\0", off, off + maxlen)
    if end < 0:
        return None
    s = data[off:end]
    if not s or any(c < 0x20 or c > 0x7E for c in s):
        return None
    return s.decode()


def word(off):
    return struct.unpack_from("<I", data, off)[0] if off + 4 <= len(data) else 0


# Walk 12-byte records from the start of the command table.  A record is over
# when the first word is not a pointer into the image, or the second is not a
# pointer into the text.
START = 0x2fb0
rows = []
p = START
while p + 12 <= len(data):
    name_p, handler, flags = struct.unpack_from("<III", data, p)
    name = text(name_p - BASE) if BASE <= name_p < END else None
    if name is None or not BASE <= handler < END:
        break
    rows.append((p, name, handler, flags))
    p += 12

print("%-18s %-9s %-11s %s" % ("command", "flags", "handler", "file off"))
print("-" * 52)
for p, name, handler, flags in rows:
    print("%-18s 0x%02x      0x%08x   0x%05x" % (name, flags, handler,
                                                   handler - BASE))

print("\n%d commands, table at file offset 0x%x" % (len(rows), START))

# Where each handler sits tells us what kind of thing it is: the menu and the
# flash work is clustered, and so is the WiFi.
buckets = {}
for p, name, handler, flags in rows:
    buckets.setdefault(handler >> 16, []).append(name)
print("\nby handler region:")
for hi in sorted(buckets):
    print("  0x95%02xxxx  %s" % (hi, ", ".join(buckets[hi])))