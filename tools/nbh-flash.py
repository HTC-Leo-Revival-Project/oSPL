#!/usr/bin/env python3
"""
flash an NBH into the phone, through the bootloader, over the console.

    tools/nbh-flash.py <file.nbh> [--emu <dir>] [--nand <img>] [--no-flash]

This is the whole flashing round trip: start the machine with the SPL in
place, hold volume-down so it comes up in the bootloader, hand it the file,
and let it write the area into the NAND.

The interesting part is that nothing here knows the format.  It sends the
command `recv`, then the file's bytes exactly as they are, and the bootloader
does the parsing - the same way the real one does it, off the file rather
than off something this script worked out in advance.

The NAND image given is copied first, so a failed flash costs a copy rather
than the original.  With --no-flash the file is taken in and described but
not written, which is how to check a file without erasing anything.

The exit status is 0 if the bootloader verified what it wrote and then found
an image it could boot, and 1 if it did not.
"""
import os
import shutil
import socket
import subprocess
import sys
import time

argv = sys.argv[1:]
if not argv or argv[0].startswith("-"):
    sys.exit(__doc__.strip())

nbh_path = argv[0]
argv = argv[1:]

spl_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
emu = os.environ.get("EMU", os.path.join(os.path.dirname(spl_dir),
                                        "qemu-hd2"))
nand = None
do_flash = True
keep = None

while argv:
    a = argv.pop(0)
    if a == "--emu" and argv:
        emu = argv.pop(0)
    elif a == "--nand" and argv:
        nand = argv.pop(0)
    elif a == "--no-flash":
        do_flash = False
    elif a == "--keep" and argv:
        keep = argv.pop(0)

if nand is None:
    nand = os.path.join(emu, "images", "nand.img")

spl_bin = os.path.join(spl_dir, "build", "spl.bin")
if not os.path.exists(spl_bin):
    sys.exit("%s does not exist - run make first" % spl_bin)
if not os.path.exists(nand):
    sys.exit("%s does not exist" % nand)
data = open(nbh_path, "rb").read()
print("nbh-flash: file   %s (%d bytes)" % (nbh_path, len(data)))
print("nbh-flash: SPL    %s" % spl_bin)
print("nbh-flash: NAND   %s" % nand)

# The write erases a block, so the original image is never the one at risk.
#
# The copy goes in the project's own build directory rather than /tmp: a NAND
# image is half a gigabyte and /tmp here is a tmpfs with a quota on it, which
# turns into a failed copy halfway through and an image that is neither the
# original nor a copy of it.
workdir = keep or os.path.join(spl_dir, "build", "flash")
os.makedirs(workdir, exist_ok=True)
work = os.path.join(workdir, "nand.img")
if keep:
    print("nbh-flash: working on %s, keeping it" % work)
else:
    print("nbh-flash: working on a copy at %s" % work)
shutil.copy(nand, work)

sock_path = os.path.join(os.path.dirname(work), "console.sock")
qemu = os.path.join(emu, "src", "qemu", "build", "qemu-system-arm")
print("nbh-flash: machine %s, volume-down held" % qemu)

proc = subprocess.Popen(
    [qemu, "-L", os.path.join(emu, "src", "qemu", "pc-bios"),
     "-M", "htcleo,keys=voldown,spl=%s" % spl_bin,
     "-drive", "if=mtd,format=raw,file=%s" % work,
     "-display", "none",
     "-serial", "unix:%s,server=on,wait=on" % sock_path],
    cwd=os.path.join(emu, "images"),
    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

log = open(os.path.join(os.path.dirname(work), "console.log"), "wb")
seen = bytearray()


def say(*a):
    print("nbh-flash:", *a, flush=True)


def pump(seconds):
    end = time.time() + seconds
    while time.time() < end:
        s.settimeout(0.05)
        try:
            b = s.recv(8192)
        except socket.timeout:
            continue
        except OSError:
            return False
        if not b:
            return False
        log.write(b)
        log.flush()
        seen.extend(b)
    return True


def send_and_drain(chunk, limit):
    """send `chunk` bytes while reading the console as it goes.

    This has to drain while sending.  The bootloader cannot take another byte
    until whatever it is printing has been read, and the socket buffers at
    both ends are far smaller than a 128 KB image - so sending it in one go
    and reading afterwards deadlocks on a full pipe.
    """
    end = time.time() + limit
    off = 0
    while off < len(chunk):
        s.settimeout(0.0)
        try:
            s.sendall(chunk[off:off + 4096])
            off += 4096
        except (BlockingIOError, socket.timeout):
            pass
        except OSError as e:
            say("the console went away while sending: %s" % e)
            return False
        # and let the bootloader have the other end of the pipe
        pump(0.02)
        if time.time() > end:
            say("gave up sending after %d of %d bytes" % (off, len(chunk)))
            return False
    return True


def wait_for(needle, limit):
    end = time.time() + limit
    while time.time() < end:
        if needle in seen:
            return True
        if not pump(0.25):
            break
    return needle in seen


rc = 1
try:
    deadline = time.time() + 30
    while True:
        try:
            s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            s.connect(sock_path)
            break
        except (FileNotFoundError, ConnectionRefusedError):
            if time.time() > deadline:
                sys.exit("nbh-flash: the machine never opened %s" % sock_path)
            time.sleep(0.1)

    say("waiting for the prompt")
    if not wait_for(b"BL> ", 60):
        say("no prompt; the console said:")
        sys.stdout.write(bytes(seen).decode("latin1"))
        sys.exit(1)

    # 1. hand it the file.  The bootloader reads exactly `len` bytes, so the
    #    length is the file's length - no framing, no escaping.
    del seen[:]
    s.sendall(("recv 0x4000000 %d\r" % len(data)).encode())
    if not wait_for(b"send them now", 20):
        say("it did not ask for the file")
        sys.stdout.write(bytes(seen).decode("latin1"))
        sys.exit(1)
    pump(0.4)
    say("sending %d bytes" % len(data))
    if not send_and_drain(data, 600):
        sys.exit(1)

    if not wait_for(b"recv: %d bytes" % len(data), 180):
        say("the file did not all arrive; it said:")
        sys.stdout.write(bytes(seen).decode("latin1"))
        sys.exit(1)
    say("the file arrived whole")

    # 2. what does it think of it?
    del seen[:]
    s.sendall(b"nbhinfo 0x4000000 %d\r" % len(data))
    wait_for(b"BL> ", 30)
    out = bytes(seen).decode("latin1")
    for line in out.replace("\r", "").split("\n"):
        if line.strip():
            say("  " + line.strip())
    ok = "LEOIMG.nbh - OK" in out
    if not ok:
        say("it would not take the file; stopping before the write")
        sys.exit(1)

    if not do_flash:
        say("--no-flash: leaving the flash alone")
        rc = 0
        sys.exit(rc)

    # 3. write it.  The bootloader asks before it erases.
    del seen[:]
    s.sendall(b"nbhflash 0x4000000 %d\r" % len(data))
    if not wait_for(b"type 'yes' to go", 30):
        say("it did not ask to go ahead")
        sys.stdout.write(bytes(seen).decode("latin1"))
        sys.exit(1)
    s.sendall(b"yes\r")
    if not wait_for(b"verified", 300):
        say("the write did not verify; it said:")
        sys.stdout.write(bytes(seen).decode("latin1"))
        sys.exit(1)

    pump(0.5)
    out = bytes(seen).decode("latin1")
    for line in out.replace("\r", "").split("\n"):
        if line.strip():
            say("  " + line.strip())

    # 4. and the flash has to read back as something it will boot.
    del seen[:]
    s.sendall(b"checkimage\r")
    wait_for(b"BL> ", 60)
    out = bytes(seen).decode("latin1")
    say("checkimage:")
    for line in out.replace("\r", "").split("\n"):
        if line.strip():
            say("  " + line.strip())
    # "the OS area reads" is what checkimage says when the area is there and
    # holds an image.  Anything else - a failure, or no answer at all - is not
    # a pass.
    rc = 0 if "the OS area reads" in out and "FAIL" not in out else 1

    say("done - %s" % ("the flash reads back as an image to boot"
                       if rc == 0 else "but the flash does not read back"))
finally:
    try:
        s.setblocking(True)
        pump(0.5)
    except OSError:
        pass
    try:
        s.close()
    except OSError:
        pass
    log.close()

    # SIGTERM, not SIGKILL.  The NAND image is a mapped file that QEMU writes
    # back as it goes, and killing it outright loses whatever has not been
    # flushed - which shows up as a flash that did not take, having taken.
    proc.terminate()
    try:
        proc.wait(timeout=30)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait()
        print("nbh-flash: had to kill it; the image may not be complete")
    print("nbh-flash: console log in %s"
          % os.path.join(workdir, "console.log"))
    if not keep:
        shutil.rmtree(workdir, ignore_errors=True)

sys.exit(rc)
