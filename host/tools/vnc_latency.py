#!/usr/bin/env python3
"""Time how long the target takes to show the result of input, with no viewer in between (the agent's share of lag).

    python host/tools/vnc_latency.py HOST[:PORT] PASSWORD [ROUNDS]     (PORT default 5901)
    python host/tools/vnc_latency.py - - [ROUNDS]                      the agent, from the settings

A - for HOST takes ON_HOST and ON_VNC_PORT, a - for PASSWORD takes ON_VNC_PASSWORD (host/tools/on_env.py).

Connects to the OpticNerve agent (RFB 3.8, VNC password, raw encoding, 32 bits per pixel) and acts like a viewer
driven by a hand on the demo panel (agent/demo/demo_panel.c): it moves the pointer to +1 along a curved path at about
60 pointer events a second (0.5 to 0.8 s, ease-in and ease-out, as host/qemu tests do through a real viewer), clicks,
then does the same to LAMP; one untimed +1 (so that RESET changes something), RESET, then ROUNDS rounds (default 10). A reader thread keeps an incremental
FramebufferUpdateRequest outstanding, as a viewer does, and notes the arrival time of every update rectangle.
For each click it reports the time from the button release to the first update that covers the count box (+1,
RESET) or the lamp (LAMP), which is the target's and the network's share of the delay a person sees in a viewer.
Takes input ownership while connected (the agent gives input to one client at a time), so no other client may be
driving the target. Needs cryptography.
"""
import math, os, random, socket, struct, sys, threading, time, traceback

try:
    from cryptography.hazmat.decrepit.ciphers.algorithms import TripleDES
except ImportError:  # cryptography < 43, e.g. Ubuntu 24.04's 41.0.7: the same class, older place
    from cryptography.hazmat.primitives.ciphers.algorithms import TripleDES
from cryptography.hazmat.primitives.ciphers import Cipher, modes
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import on_env
on_env.load()


def recvn(s, n):
    b = bytearray()
    while len(b) < n:
        c = s.recv(min(n - len(b), 1 << 20))
        if not c:
            raise EOFError("connection closed")
        b += c
    return bytes(b)


def connect(host, password):
    host, port = on_env.vnc_address(host, 5901)
    password = on_env.vnc_password(password)
    s = socket.create_connection((host, port), timeout=30)
    s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    recvn(s, 12)
    s.sendall(b"RFB 003.008\n")
    types = recvn(s, recvn(s, 1)[0])
    if 2 not in types:
        sys.exit(f"server does not offer VNC password authentication: {list(types)}")
    s.sendall(bytes([2]))
    key = bytes(int(f"{b:08b}"[::-1], 2) for b in password.encode().ljust(8, b"\0")[:8])
    enc = Cipher(TripleDES(key * 3), modes.ECB()).encryptor()
    s.sendall(enc.update(recvn(s, 16)) + enc.finalize())
    if struct.unpack(">I", recvn(s, 4))[0]:
        sys.exit("authentication failed")
    s.sendall(bytes([1]))  # shared
    w, h = struct.unpack(">HH", recvn(s, 4))
    recvn(s, 16)
    recvn(s, struct.unpack(">I", recvn(s, 4))[0])
    # 32 bpp little-endian true colour, raw encoding only
    s.sendall(struct.pack(">BxxxBBBBHHHBBBxxx", 0, 32, 24, 0, 1, 255, 255, 255, 16, 8, 0))
    s.sendall(struct.pack(">BxHi", 2, 1, 0))
    return s, w, h


COUNT_BOX = (160, 280, 700, 180)   # demo_panel.c: the count box and the lamp
LAMP_BOX = (1100, 280, 180, 180)
TARGETS = {"+1": (370, 800), "RESET": (960, 800), "LAMP": (1550, 800)}


def covers(r, box):
    x, y, w, h = r
    bx, by, bw, bh = box
    return x < bx + bw and bx < x + w and y < by + bh and by < y + h


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    rounds = int(sys.argv[3]) if len(sys.argv) > 3 else 10
    s, W, H = connect(sys.argv[1], sys.argv[2])
    log = []            # (arrival time, (x, y, w, h)) of every rectangle
    lock = threading.Lock()
    send_lock = threading.Lock()

    def send(b):
        with send_lock:
            s.sendall(b)

    reader_error = []

    def reader():
        try:
            read_loop()
        except Exception as e:  # noqa: BLE001 - reported, and the run stops at the next click
            if not closing:
                reader_error.append("".join(traceback.format_exception(e)).strip())

    def read_loop():
        send(struct.pack(">BBHHHH", 3, 0, 0, 0, W, H))  # one full frame first
        while True:
            t = recvn(s, 1)[0]
            if t == 0:
                n = struct.unpack(">xH", recvn(s, 3))[0]
                for _ in range(n):
                    x, y, w, h, enc = struct.unpack(">HHHHi", recvn(s, 12))
                    if enc != 0:
                        raise SystemExit(f"unexpected encoding {enc}")
                    recvn(s, w * h * 4)
                    with lock:
                        log.append((time.time(), (x, y, w, h)))
                send(struct.pack(">BBHHHH", 3, 1, 0, 0, W, H))  # incremental, as a viewer asks
            elif t == 1:
                recvn(s, 5)
                recvn(s, 6 * struct.unpack(">H", recvn(s, 2))[0])
            elif t == 2:
                pass
            elif t == 3:
                recvn(s, 3)
                recvn(s, struct.unpack(">I", recvn(s, 4))[0])
            else:
                raise SystemExit(f"unexpected server message {t}")

    closing = False
    s.settimeout(None)  # the reader waits as long as the screen stays still; act() has its own 30 s limit
    threading.Thread(target=reader, daemon=True).start()
    time.sleep(2)
    random.seed(1)
    pos = [960, 1000]

    def ptr(buttons, x, y):
        send(struct.pack(">BBHH", 5, buttons, max(0, min(W - 1, int(x))), max(0, min(H - 1, int(y)))))

    def hand_move(x1, y1):
        x0, y0 = pos
        dist = math.hypot(x1 - x0, y1 - y0)
        dur = 0.5 + 0.3 * min(1.0, dist / 1500)
        bend = random.uniform(-0.15, 0.15) * dist
        nx, ny = (-(y1 - y0) / dist, (x1 - x0) / dist) if dist else (0, 0)
        cx, cy = (x0 + x1) / 2 + nx * bend, (y0 + y1) / 2 + ny * bend
        steps = max(2, int(dur * 60))
        for i in range(1, steps + 1):
            t = i / steps
            e = t * t * (3 - 2 * t)
            x = (1 - e) ** 2 * x0 + 2 * (1 - e) * e * cx + e * e * x1
            y = (1 - e) ** 2 * y0 + 2 * (1 - e) * e * cy + e * e * y1
            if i < steps:
                x += random.uniform(-1.5, 1.5)
                y += random.uniform(-1.5, 1.5)
            ptr(0, x, y)
            time.sleep(dur / steps)
        pos[:] = [x1, y1]

    results = []

    def act(name, timed=True):
        if reader_error:
            sys.exit("the reader stopped, so no frame after this point can be timed:\n" + reader_error[0])
        gx, gy = TARGETS[name]
        tx, ty = gx + random.uniform(-60, 60), gy + random.uniform(-40, 40)
        hand_move(tx, ty)
        time.sleep(random.uniform(0.08, 0.2))
        ptr(1, tx, ty)
        time.sleep(0.05 + random.uniform(0, 0.04))
        ptr(0, tx, ty)
        t_up = time.time()
        box = LAMP_BOX if name == "LAMP" else COUNT_BOX
        seen = None
        while time.time() - t_up < 30:
            with lock:
                hit = [t for t, r in log if t >= t_up and covers(r, box)]
            if hit:
                seen = round(min(hit) - t_up, 3)
                break
            time.sleep(0.01)
        if timed:
            results.append((name, seen))
        print(f"{name:5} shown after {seen} s" + ("" if timed else " (not counted)"), flush=True)
        time.sleep(0.3)

    def nonlocal_close():
        nonlocal closing
        closing = True
        s.close()

    act("+1", timed=False)  # so that RESET has something to reset (a fresh panel is already at 0)
    act("RESET")
    for _ in range(rounds):
        act("+1")
        act("LAMP")
    lat = sorted(r[1] for r in results if r[1] is not None)
    print(f"actions {len(results)}, shown {len(lat)}, missed {len(results) - len(lat)}, "
          f"median {lat[len(lat) // 2] if lat else None} s, max {lat[-1] if lat else None} s")
    nonlocal_close()


if __name__ == "__main__":
    main()
