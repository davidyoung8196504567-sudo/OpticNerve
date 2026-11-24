#!/usr/bin/env python3
"""Robustness tests of the OpticNerve server over the network (tests RB01, RB03, HS09 and others of the private
test outline). Raw RFB connections only; sends no key or pointer events.

    python host/tests/robustness.py [HOST[:PORT]]        (PORT default 5901)

Without HOST (or with -), the agent from the settings: ON_HOST and ON_VNC_PORT (host/tools/on_env.py: the
environment, then the settings file). Needs ON_VNC_PASSWORD (the agent's password) and the agent running (host/tools/opticnerve_deploy.py start).
Each test prints PASS or FAIL with what it saw; the exit status is the number of failures.
Needs the cryptography package, as host/tools/vnc_snapshot.py does.
"""
import os, random, socket, struct, sys, time
try:
    from cryptography.hazmat.decrepit.ciphers.algorithms import TripleDES
except ImportError:  # cryptography < 43, e.g. Ubuntu 24.04's 41.0.7: the same class, older place
    from cryptography.hazmat.primitives.ciphers.algorithms import TripleDES
from cryptography.hazmat.primitives.ciphers import Cipher, modes
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools"))
import on_env
on_env.load()

HOST, PORT = on_env.vnc_address(sys.argv[1] if len(sys.argv) > 1 else "-", 5901)
PW = os.environ.get("ON_VNC_PASSWORD", "").encode()
fails = 0


def check(name, ok, detail=""):
    global fails
    fails += not ok
    print(f"{'PASS' if ok else 'FAIL'}  {name}  {detail}")


def recvn(s, n):
    b = b""
    while len(b) < n:
        c = s.recv(n - len(b))
        if not c:
            raise EOFError("closed")
        b += c
    return b


def connect(timeout=10):
    s = socket.create_connection((HOST, PORT), timeout=timeout)
    return s


def response(challenge, pw=None):
    key = bytes(int(f"{b:08b}"[::-1], 2) for b in (PW if pw is None else pw).ljust(8, b"\0")[:8])
    enc = Cipher(TripleDES(key * 3), modes.ECB()).encryptor()
    return enc.update(challenge) + enc.finalize()


def login(pw=None, timeout=10):
    """Returns (socket, width, height), or raises with the server's reason."""
    s = connect(timeout)
    recvn(s, 12)
    s.sendall(b"RFB 003.008\n")
    n = recvn(s, 1)[0]
    if n == 0:
        raise RuntimeError(recvn(s, struct.unpack(">I", recvn(s, 4))[0]).decode())
    recvn(s, n)
    s.sendall(bytes([2]))
    s.sendall(response(recvn(s, 16), pw))
    if struct.unpack(">I", recvn(s, 4))[0]:
        raise RuntimeError(recvn(s, struct.unpack(">I", recvn(s, 4))[0]).decode())
    s.sendall(bytes([1]))
    w, h = struct.unpack(">HH", recvn(s, 4))
    recvn(s, 16)
    recvn(s, struct.unpack(">I", recvn(s, 4))[0])
    return s, w, h


def frame(s, w, h, incremental=0, x=0, y=0):
    """Requests an area and reads one update; returns (rectangles, seconds)."""
    t0 = time.time()
    s.sendall(struct.pack(">BBHHHH", 3, incremental, x, y, w, h))
    t = recvn(s, 1)[0]
    assert t == 0, f"message type {t}"
    recvn(s, 1)
    count = struct.unpack(">H", recvn(s, 2))[0]
    for _ in range(count):
        rx, ry, rw, rh, enc = struct.unpack(">HHHHi", recvn(s, 12))
        assert enc == 0, f"encoding {enc}"
        recvn(s, rw * rh * 4)
    return count, time.time() - t0


def alive(name):
    try:
        s, w, h = login()
        n, dt = frame(s, w, h)
        s.close()
        check(name, n > 0, f"full frame {n} tiles in {dt:.2f} s")
    except Exception as e:
        check(name, False, repr(e))


def close_at_random_points(count=200, seed=1, wait_limit=35.0):
    """RB01 (connect and disconnect in every state), over the network: every state by list, then random ones.

    A connection the agent turns away (closed at accept, or "server full") because the slots of earlier connections
    are still taken is tried again every 0.25 s, for up to wait_limit seconds. From some clients (macOS, with its
    default limit on resets) a connection closed by reset can stay open until the agent's keepalive ends it, about
    3 s, and its slot with it. Every wait is reported; a slot that is not free within wait_limit is a failure."""
    rnd = random.Random(seed)
    states = ["accept", "after version", "after security type", "after challenge", "after response",
              "after ClientInit", "mid request", "mid frame"]
    plan = states + [rnd.choice(states) for _ in range(count)]
    waits = []
    for i, st in enumerate(plan):
        t0 = time.time()
        while True:
            try:
                one_connection(rnd, st)
                break
            except (EOFError, ConnectionResetError) as e:
                if time.time() - t0 > wait_limit:
                    check(f"RB01 connection {i} ({st})", False, f"{e!r}; no free slot within {wait_limit:.0f} s")
                    return
                time.sleep(0.25)
            except Exception as e:
                check(f"RB01 connection {i} ({st})", False, repr(e))
                return
        if time.time() - t0 > 0.2:
            waits.append((i, st, time.time() - t0))
        time.sleep(0.02)
    for i, st, w in waits:
        print(f"      RB01 connection {i} ({st}) waited {w:.1f} s for a free slot")
    check(f"RB01 {len(plan)} connections closed at every state (half by reset)", True,
          f"{len(waits)} waited for a free slot, longest {max((w for _, _, w in waits), default=0):.1f} s")
    alive("RB01 afterwards: a new client gets a full frame")


def one_connection(rnd, st):
    """One RB01 connection: taken to state ST, then closed, by reset or orderly as RND decides (one draw per
    connection that gets through, so a connection tried again keeps the plan's sequence)."""
    s = connect()
    try:
        if st != "accept":
            recvn(s, 12)
            s.sendall(b"RFB 003.008\n")
        if st not in ("accept", "after version"):
            recvn(s, 2)
            s.sendall(bytes([2]))
        if st not in ("accept", "after version", "after security type"):
            ch = recvn(s, 16)
        if st in ("after response", "after ClientInit", "mid request", "mid frame"):
            s.sendall(response(ch))
            recvn(s, 4)
        if st in ("after ClientInit", "mid request", "mid frame"):
            s.sendall(bytes([1]))
            w, h = struct.unpack(">HH", recvn(s, 4))
            recvn(s, 16)
            recvn(s, struct.unpack(">I", recvn(s, 4))[0])
        if st == "mid request":
            s.sendall(bytes([3, 0, 0]))  # a request cut after 3 of its 10 bytes
        if st == "mid frame":
            s.sendall(struct.pack(">BBHHHH", 3, 0, 0, 0, w, h))
            recvn(s, 4096)  # part of the update, then go
    except BaseException:
        s.close()
        raise
    if rnd.random() < 0.5:  # half of them by reset instead of an orderly close
        s.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("HH" if os.name == "nt" else "ii", 1, 0))
    s.close()


def handshake_slots():
    """Two connections that say nothing hold the two handshake slots; a third is closed at accept."""
    idle = [connect(), connect()]
    for s in idle:
        recvn(s, 12)
    time.sleep(0.3)
    t = connect()
    try:
        t.settimeout(3)
        got = t.recv(12)
        check("handshake slots: a third silent connection is closed at accept", got == b"", f"got {got!r}")
    except Exception as e:
        check("handshake slots: a third silent connection is closed at accept", isinstance(e, ConnectionResetError), repr(e))
    t.close()
    print("      waiting 11 s for the handshake deadline (10 s)")
    time.sleep(11)
    closed = 0
    for s in idle:
        try:
            s.settimeout(2)
            closed += s.recv(1) == b""
        except ConnectionResetError:
            closed += 1
        except socket.timeout:
            pass
        s.close()
    check("handshake deadline closes silent connections", closed == 2, f"{closed} of 2 closed")
    alive("after the deadline a client logs in again")


def fifth_client():
    """HS09: four logged-in clients; a fifth with the right password is told "server full"; the four keep working."""
    four = [login() for _ in range(4)]
    try:
        login()
        check("HS09 fifth client refused", False, "it logged in")
    except RuntimeError as e:
        check("HS09 fifth client refused", "full" in str(e), f"reason {str(e)!r}")
    ok = all(frame(s, w, h)[0] > 0 for s, w, h in four)
    check("HS09 the four keep their frames", ok)
    for s, _, _ in four:
        s.close()


def wrong_password():
    try:
        login(pw=b"wrong")
        check("wrong password refused", False, "it logged in")
    except RuntimeError as e:
        check("wrong password refused with a reason", "authentication" in str(e), repr(str(e)))


def stalled_client():
    """RB03: a client that stops reading in the middle of a full frame does not delay another client."""
    st = socket.socket()
    st.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4096)
    st.settimeout(10)
    st.connect((HOST, PORT))
    recvn(st, 12)
    st.sendall(b"RFB 003.008\n")
    recvn(st, 2)
    st.sendall(bytes([2]))
    st.sendall(response(recvn(st, 16)))
    recvn(st, 4)
    st.sendall(bytes([1]))
    w, h = struct.unpack(">HH", recvn(st, 4))
    recvn(st, 16)
    recvn(st, struct.unpack(">I", recvn(st, 4))[0])
    st.sendall(struct.pack(">BBHHHH", 3, 0, 0, 0, w, h))
    recvn(st, 1000)  # then stop reading
    time.sleep(1)
    s, w, h = login()
    worst = 0
    for _ in range(5):
        n, dt = frame(s, w, h)
        worst = max(worst, dt)
    check("RB03 a stalled client does not delay another", worst < 2.0, f"slowest full frame {worst:.2f} s")
    # The agent counts the 30 s from the last time output to the client moved, not from when the client stopped
    # reading: over Wi-Fi output kept moving into the stalled connection for about 5 s after it stopped reading. Reading
    # before the agent has closed it ends the stall, so the wait leaves room for that.
    print("      waiting 40 s for the stall deadline (30 s after output last moved)")
    time.sleep(40)
    drained = 0
    try:
        st.settimeout(5)
        while True:
            c = st.recv(65536)
            if not c:
                break
            drained += len(c)
        check("RB03 the stalled client is closed after 30 s", True, f"closed after {drained} more bytes")
    except Exception as e:
        check("RB03 the stalled client is closed after 30 s", isinstance(e, ConnectionResetError),
              f"{e!r} after {drained} more bytes")
    st.close()
    n, dt = frame(s, w, h)
    check("RB03 the other client still works", n > 0, f"{dt:.2f} s")
    s.close()


def garbage():
    """An unknown message type closes that client only."""
    s, w, h = login()
    s.sendall(bytes([99, 1, 2, 3]))
    s.settimeout(3)
    try:
        closed = s.recv(1) == b""
    except ConnectionResetError:
        closed = True
    except socket.timeout:
        closed = False
    check("HS07 unknown message type closes that client", closed)
    s.close()
    alive("HS07 the server keeps serving")


if __name__ == "__main__":
    if not PW:
        sys.exit("set ON_VNC_PASSWORD")
    print(f"robustness tests against {HOST}:{PORT}")
    alive("before: a client gets a full frame")
    wrong_password()
    garbage()
    fifth_client()
    handshake_slots()
    stalled_client()
    close_at_random_points()
    print(f"{fails} failed")
    sys.exit(fails)
