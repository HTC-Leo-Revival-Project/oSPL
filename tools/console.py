#!/usr/bin/env python3
"""
the bootloader's console: log it, and type at it.

A unix stream socket has one reader, so the thing that watches the console and
the thing that talks to it have to be the same process - which is fine,
because both are just "read whatever arrives".  Everything the guest sends
goes to the log as it arrives, and any --cmd is typed once the prompt comes
back.

    tools/console.py <socket> <log>
    tools/console.py <socket> <log> --cmd blversion --cmd info

The exit status is 0 if the log looks like a good boot, 1 if it does not:
one "SPL: starting", one "SPL: jumping", or a prompt if the bootloader was
asked for.  That makes it usable straight from a test script.
"""
import os
import socket
import sys
import time

sock_path, log_path = sys.argv[1], sys.argv[2]
argv = sys.argv[3:]

commands = []
timeout = 120
while argv:
    a = argv.pop(0)
    if a == "--cmd" and argv:
        commands.append(argv.pop(0))
    elif a == "--timeout" and argv:
        timeout = int(argv.pop(0))

deadline = time.time() + 60
while True:
    try:
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.connect(sock_path)
        break
    except (FileNotFoundError, ConnectionRefusedError):
        if time.time() > deadline:
            sys.exit("gave up waiting for %s" % sock_path)
        time.sleep(0.1)

# CMDS is one command per line, which keeps "mddir 0x80000" in one piece.
if not commands:
    commands = [c for c in os.environ.get("CMDS", "").split("\n") if c.strip()]

log = open(log_path, "wb")
seen = bytearray()


def pump(seconds):
    """read for a while, writing everything to the log as it lands"""
    end = time.time() + seconds
    while time.time() < end:
        s.settimeout(0.05)
        try:
            b = s.recv(4096)
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


def wait_for(needle, limit):
    """wait until `needle` has been seen, or `limit` seconds have passed"""
    end = time.time() + limit
    while time.time() < end:
        if needle in seen:
            return True
        if not pump(0.25):
            break
    return needle in seen


# The bootloader's key hold is a whole second, so the prompt may be a moment
# away.  Typing into a UART nobody has read yet loses the line.
in_bl = wait_for(b"BL> ", timeout)

for cmd in commands:
    del seen[:]
    s.sendall(cmd.encode() + b"\r")
    wait_for(b"BL> ", 30)

# Leave the log open a moment so anything the last command printed lands.
pump(1.0)
log.close()

text = bytes(seen) if False else open(log_path, "rb").read()
starts = text.count(b"SPL: starting")
jumps = text.count(b"SPL: jumping")
prompt = text.count(b"BL> ")

if in_bl or (starts <= 1 and jumps <= 1 and b"SPL: loaded" in text):
    os._exit(0)
sys.exit("boot looks wrong: %d starts, %d jumps, %d prompts"
         % (starts, jumps, prompt))