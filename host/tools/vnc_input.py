#!/usr/bin/env python3
"""Send keyboard and pointer input to a VNC (RFB 3.8) server.

    python host/tools/vnc_input.py HOST PASSWORD ACTION...

Actions, done in order:
    text:STRING        type the characters (key press and release each)
    key:KEYSYM         press and release one X11 keysym, e.g. key:0xff08 (BackSpace), key:0xff0d (Return)
    combo:KS+KS...     hold the keysyms in order, release in reverse, e.g. combo:0xffe9+0xffff (Alt+Delete)
    wait:SECONDS       pause
    move:X,Y           move the pointer, no buttons
    click:X,Y          left click at X,Y
    drag:X1,Y1,X2,Y2   left-button drag in ten steps

Only the left button is used: in X-style terminals the middle button pastes.
Needs cryptography.
"""
import socket, struct, sys, time
from cryptography.hazmat.decrepit.ciphers.algorithms import TripleDES
from cryptography.hazmat.primitives.ciphers import Cipher, modes


def recvn(s, n):
    b = b""
    while len(b) < n:
        c = s.recv(n - len(b))
        if not c:
            raise EOFError("connection closed")
        b += c
    return b


def connect(host, password):
    s = socket.create_connection((host, 5900), timeout=10)
    recvn(s, 12)
    s.sendall(b"RFB 003.008\n")
    types = recvn(s, recvn(s, 1)[0])
    if 2 not in types:
        sys.exit(f"server does not offer VNC password authentication: {list(types)}")
    s.sendall(bytes([2]))
    # VNC authentication: DES with each password byte's bits reversed.
    key = bytes(int(f"{b:08b}"[::-1], 2) for b in password.encode().ljust(8, b"\0")[:8])
    enc = Cipher(TripleDES(key * 3), modes.ECB()).encryptor()
    s.sendall(enc.update(recvn(s, 16)) + enc.finalize())
    if struct.unpack(">I", recvn(s, 4))[0]:
        sys.exit("authentication failed")
    s.sendall(bytes([1]))  # ClientInit: shared, don't disconnect other viewers
    recvn(s, 20)
    recvn(s, struct.unpack(">I", recvn(s, 4))[0])
    return s


def key(s, keysym):
    for down in (1, 0):
        s.sendall(struct.pack(">BBxxI", 4, down, keysym))
        time.sleep(0.05)


def pointer(s, buttons, x, y):
    s.sendall(struct.pack(">BBHH", 5, buttons, x, y))
    time.sleep(0.05)


def main():
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    s = connect(sys.argv[1], sys.argv[2])
    for action in sys.argv[3:]:
        kind, _, arg = action.partition(":")
        if kind == "text":
            for ch in arg:
                key(s, ord(ch))
        elif kind == "key":
            key(s, int(arg, 0))
        elif kind == "combo":
            syms = [int(k, 0) for k in arg.split("+")]
            for down, order in ((1, syms), (0, syms[::-1])):
                for k in order:
                    s.sendall(struct.pack(">BBxxI", 4, down, k))
                    time.sleep(0.05)
        elif kind == "wait":
            time.sleep(float(arg))
        else:
            v = [int(n) for n in arg.split(",")]
            if kind == "move":
                pointer(s, 0, *v)
            elif kind == "click":
                pointer(s, 0, *v)
                pointer(s, 1, *v)
                pointer(s, 0, *v)
            elif kind == "drag":
                x1, y1, x2, y2 = v
                pointer(s, 0, x1, y1)
                for i in range(11):
                    pointer(s, 1, x1 + (x2 - x1) * i // 10, y1 + (y2 - y1) * i // 10)
                pointer(s, 0, x2, y2)
            else:
                sys.exit(f"unknown action: {action}")
        print("sent", action)
    time.sleep(0.5)  # let the server process the events before the socket closes
    s.close()


if __name__ == "__main__":
    main()
