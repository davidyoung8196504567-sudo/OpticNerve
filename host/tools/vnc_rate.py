#!/usr/bin/env python3
"""Measure a VNC (RFB 3.8) server's update rate with a viewer that stays connected (spike S6).

    python host/tools/vnc_rate.py HOST PASSWORD SECONDS [ENCODINGS]

View-only, like vnc_snapshot.py: one full frame first, then incremental FramebufferUpdateRequests
for SECONDS, printing updates, rectangles and bytes per second. ENCODINGS is a comma-separated
list of RFB encoding numbers to offer (default 0 = raw); the tool decodes nothing, it only counts,
so it can only offer encodings whose length it can read: 0 (raw) and 16 (ZRLE). Needs cryptography.
"""
import socket, struct, sys, time
from cryptography.hazmat.decrepit.ciphers.algorithms import TripleDES
from cryptography.hazmat.primitives.ciphers import Cipher, modes

def recvn(s, n):
    b = bytearray()
    while len(b) < n:
        c = s.recv(min(n - len(b), 1 << 20))
        if not c: raise EOFError("closed")
        b += c
    return bytes(b)

host, pw, secs = sys.argv[1], sys.argv[2].encode(), float(sys.argv[3])
encodings = [int(e) for e in (sys.argv[4] if len(sys.argv) > 4 else "0").split(",")]
assert set(encodings) <= {0, 16}, "only raw (0) and ZRLE (16) can be counted"
s = socket.create_connection((host, 5900), timeout=10)
recvn(s, 12); s.sendall(b"RFB 003.008\n")
n = recvn(s, 1)[0]; types = list(recvn(s, n)); assert 2 in types, types
s.sendall(bytes([2]))
challenge = recvn(s, 16)
key = bytes(int(f"{b:08b}"[::-1], 2) for b in pw.ljust(8, b"\0")[:8])   # VNC auth reverses each key byte's bits
enc = Cipher(TripleDES(key * 3), modes.ECB()).encryptor()
s.sendall(enc.update(challenge) + enc.finalize())
if struct.unpack(">I", recvn(s, 4))[0]: sys.exit("auth: FAILED")
s.sendall(bytes([1]))                                   # ClientInit: shared session
w, h = struct.unpack(">HH", recvn(s, 4)); recvn(s, 16); recvn(s, struct.unpack(">I", recvn(s, 4))[0])
s.sendall(struct.pack(">BxxxBBBBHHHBBBxxx", 0, 32, 24, 0, 1, 255, 255, 255, 16, 8, 0))
s.sendall(struct.pack(">BxH", 2, len(encodings)) + b"".join(struct.pack(">i", e) for e in encodings))
print(f"framebuffer {w}x{h}, encodings offered {encodings}, {secs:.0f} s")

def update(incremental):
    """Request one update and read it; return (rectangles, bytes, encodings seen)."""
    s.sendall(struct.pack(">BBHHHH", 3, incremental, 0, 0, w, h))
    while True:
        t = recvn(s, 1)[0]
        if t == 0: break
        if t == 2: continue
        if t == 1: _, _, k = struct.unpack(">xHH", recvn(s, 5)); recvn(s, 6 * k); continue
        if t == 3: recvn(s, 3); recvn(s, struct.unpack(">I", recvn(s, 4))[0]); continue
        raise RuntimeError(f"unexpected message {t}")
    rects = struct.unpack(">xH", recvn(s, 3))[0]; nbytes = 0; seen = set()
    for _ in range(rects):
        x, y, rw, rh, e = struct.unpack(">HHHHi", recvn(s, 12)); seen.add(e)
        if e == 0: n = rw * rh * 4
        elif e == 16: n = struct.unpack(">I", recvn(s, 4))[0]
        else: raise RuntimeError(f"server used encoding {e}")
        recvn(s, n); nbytes += 12 + n
    return rects, nbytes, seen

t0 = time.time(); r, b, e = update(0)
print(f"first full frame: {r} rect(s), {b / 1e6:.2f} MB, {time.time() - t0:.2f} s, encodings {sorted(e)}")
start = second = time.time(); total = cnt = rc = by = 0; seen = set()
while time.time() - start < secs:
    r, b, e = update(1); cnt += 1; rc += r; by += b; seen |= e; total += 1
    now = time.time()
    if now - second >= 1.0:
        print(f"t={now - start:5.1f}  updates {cnt:3d}  rects {rc:4d}  {by / 1e6:6.2f} MB")
        cnt = rc = by = 0; second = now
print(f"total {total} updates in {time.time() - start:.1f} s ({total / (time.time() - start):.1f}/s), encodings {sorted(seen)}")
s.close()
