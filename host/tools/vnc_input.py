#!/usr/bin/env python3
"""Send keyboard and pointer input to a VNC (RFB 3.8) server.

    python host/tools/vnc_input.py HOST[:PORT] PASSWORD ACTION...      (PORT default 5900)
    python host/tools/vnc_input.py - - ACTION...                       the agent, from the settings

A - for HOST takes ON_HOST and ON_VNC_PORT, a - for PASSWORD takes ON_VNC_PASSWORD (host/tools/on_env.py).

Actions, done in order, the way a hand does them (host/tools/hand.py: the pointer glides along a curved path at
about 60 events a second, rests, and holds the button 50 to 90 ms; keys 60 to 180 ms apart):
    text:STRING        type the characters (key press and release each)
    key:KEYSYM         press and release one X11 keysym, e.g. key:0xff08 (BackSpace), key:0xff0d (Return)
    combo:KS+KS...     hold the keysyms in order, release in reverse, e.g. combo:0xffe9+0xffff (Alt+Delete)
    wait:SECONDS       pause
    move:X,Y           move the pointer to X,Y, no buttons
    click:X,Y          move to X,Y and left click
    drag:X1,Y1,X2,Y2   move to X1,Y1, hold the left button, move to X2,Y2, release

Only the left button is used: in X-style terminals the middle button pastes. ON_HAND_SEED=N repeats a run's paths
(the seed is printed).
Needs cryptography.
"""
import os, socket, struct, sys, time
try:
    from cryptography.hazmat.decrepit.ciphers.algorithms import TripleDES
except ImportError:  # cryptography < 43, e.g. Ubuntu 24.04's 41.0.7: the same class, older place
    from cryptography.hazmat.primitives.ciphers.algorithms import TripleDES
from cryptography.hazmat.primitives.ciphers import Cipher, modes
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import on_env
from hand import Hand, SEED
on_env.load()


def recvn(s, n):
    b = b""
    while len(b) < n:
        c = s.recv(n - len(b))
        if not c:
            raise EOFError("connection closed")
        b += c
    return b


def connect(host, password):
    host, port = on_env.vnc_address(host, 5900)
    target = f"{host}:{port}"
    password = on_env.vnc_password(password)
    s = socket.create_connection((host, port), timeout=10)
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
    w, h = struct.unpack(">HH", recvn(s, 20)[:4])
    recvn(s, struct.unpack(">I", recvn(s, 4))[0])
    return s, w, h, target


def main():
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    s, w, h, target = connect(sys.argv[1], sys.argv[2])
    hand = Hand(s.sendall, w, h, target)
    print(f"hand seed {SEED}")
    for action in sys.argv[3:]:
        kind, _, arg = action.partition(":")
        if kind == "text":
            hand.keys([ord(ch) for ch in arg])
        elif kind == "key":
            hand.key(int(arg, 0))
        elif kind == "combo":
            syms = [int(k, 0) for k in arg.split("+")]
            for down, order in ((True, syms), (False, syms[::-1])):
                for k in order:
                    hand.key(k, down)
                    time.sleep(hand.rnd.uniform(0.06, 0.12))
        elif kind == "wait":
            time.sleep(float(arg))
        else:
            v = [int(n) for n in arg.split(",")]
            if kind == "move":
                hand.move(*v)
            elif kind == "click":
                hand.click(*v)
            elif kind == "drag":
                hand.drag(*v)
            else:
                sys.exit(f"unknown action: {action}")
        print("sent", action)
    time.sleep(0.5)  # let the server process the events before the socket closes
    s.close()


if __name__ == "__main__":
    main()
