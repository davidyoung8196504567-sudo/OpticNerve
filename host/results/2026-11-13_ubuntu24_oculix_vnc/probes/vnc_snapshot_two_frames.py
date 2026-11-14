#!/usr/bin/env python3
"""Grab a frame from a VNC (RFB 3.8) server, view-only, and save it as an image.

    python vnc_snapshot_two_frames.py HOST PASSWORD OUT.png [PORT]

host/tools/vnc_snapshot.py with a second full request 2 s after the first, keeping the second frame: TightVNC's
first frame is all black. Uses VNC password authentication and raw encoding. Sends no input events: only a
shared ClientInit, SetPixelFormat, SetEncodings and two FramebufferUpdateRequests.
Needs Pillow and cryptography.
"""
import socket, struct, sys, time
from cryptography.hazmat.primitives.ciphers.algorithms import TripleDES
from cryptography.hazmat.primitives.ciphers import Cipher, modes
from PIL import Image

def recvn(s, n):
    b = b""
    while len(b) < n:
        c = s.recv(n - len(b))
        if not c: raise EOFError("closed")
        b += c
    return b

host, pw, out = sys.argv[1], sys.argv[2].encode(), sys.argv[3]
s = socket.create_connection((host, int(sys.argv[4]) if len(sys.argv) > 4 else 5900), timeout=10)
print("server:", recvn(s, 12)); s.sendall(b"RFB 003.008\n")
n = recvn(s, 1)[0]; types = list(recvn(s, n)); assert 2 in types, types
s.sendall(bytes([2]))
challenge = recvn(s, 16)
key = bytes(int(f"{b:08b}"[::-1], 2) for b in pw.ljust(8, b"\0")[:8])   # VNC auth reverses each key byte's bits
enc = Cipher(TripleDES(key * 3), modes.ECB()).encryptor()
s.sendall(enc.update(challenge) + enc.finalize())
res = struct.unpack(">I", recvn(s, 4))[0]
print("auth:", "OK" if res == 0 else f"FAILED ({res})")
if res: sys.exit(1)
s.sendall(bytes([1]))                                   # ClientInit: shared session, don't kick others
w, h = struct.unpack(">HH", recvn(s, 4)); pf = recvn(s, 16); name = recvn(s, struct.unpack(">I", recvn(s, 4))[0])
print(f"framebuffer {w}x{h}, server pixel format bpp={pf[0]} depth={pf[1]}, name={name!r}")
# Ask for 32-bit true colour, little-endian, red at bit 16 / green 8 / blue 0, and raw encoding only.
s.sendall(struct.pack(">BxxxBBBBHHHBBBxxx", 0, 32, 24, 0, 1, 255, 255, 255, 16, 8, 0))
s.sendall(struct.pack(">BxHi", 2, 1, 0))
def frame():
    t0 = time.time()
    s.sendall(struct.pack(">BBHHHH", 3, 0, 0, 0, w, h))     # FramebufferUpdateRequest, full, non-incremental
    img = Image.new("RGB", (w, h))
    while True:
        t = recvn(s, 1)[0]
        if t == 0: break
        if t == 2: continue                                  # bell
        if t == 1:                                           # colour map: skip
            _, _, k = struct.unpack(">xHH", recvn(s, 5)); recvn(s, 6 * k); continue
        if t == 3: recvn(s, 3); recvn(s, struct.unpack(">I", recvn(s, 4))[0]); continue
        raise RuntimeError(f"unexpected message {t}")
    rects = struct.unpack(">xH", recvn(s, 3))[0]
    for _ in range(rects):
        x, y, rw, rh, encoding = struct.unpack(">HHHHi", recvn(s, 12))
        assert encoding == 0, encoding
        data = recvn(s, rw * rh * 4)
        img.paste(Image.frombuffer("RGB", (rw, rh), data, "raw", "BGRX"), (x, y))
    print(f"frame: {rects} rectangle(s) in {time.time() - t0:.2f} s")
    return img
# Some servers (TightVNC) send an all-black first frame: take a second one after a pause.
img = frame()
time.sleep(2)
img = frame()
img.save(out); s.close(); print("saved", out)
