#!/usr/bin/env python3
"""
read a QEMU serial chardev from its unix socket into a file.

QEMU's file: chardev is buffered and loses whatever was in the buffer when it
is killed, which is most of the console when the program under test only says
a few lines.  A socket has no buffer of its own: every byte the guest writes
arrives here, in order, and the log on disk is the whole conversation.

    ./uartlog.py <socket> <file>      until the socket goes away
"""
import os
import socket
import sys

sock_path, out_path = sys.argv[1], sys.argv[2]

while True:
    try:
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.connect(sock_path)
        break
    except (FileNotFoundError, ConnectionRefusedError):
        # QEMU creates the listener before it boots anything, but not before
        # the machine is built; give it a moment.
        import time
        time.sleep(0.1)

with open(out_path, "wb") as out:
    while True:
        try:
            b = s.recv(4096)
        except ConnectionResetError:
            break
        if not b:
            break
        out.write(b)
        out.flush()

os._exit(0)