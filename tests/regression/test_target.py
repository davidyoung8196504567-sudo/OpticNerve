"""The target's tests (marker `pi`, run with --pi): SSH, the agent, a frame, SSH through a port forward (SSH on another
address and port than VNC), the core's own test on the target, the robustness checks, the demo panel and the pointer
arrow.

    python -m pytest --pi -m pi

None of them moves the target's pointer or types, except the pointer arrow test and the mouse hold test, which are
marked `watched` and run only with --watched as well (one at a time, watched, when agreed). Both connect a viewer
that gets the arrow, so both take the target's own mouse from the person at it for a few seconds:

    python -m pytest --pi --watched -m watched

They leave the target as they found it: the robustness checks, the demo panel test and the pointer arrow test skip,
rather than disturb, while any client is connected to the agent (also one still at its password prompt) or a panel
is running. The core test works in a folder of its own on the target and deletes it.
"""
import os
import socket
import subprocess
import sys
import time
import uuid

import pytest

from conftest import ROOT, SH, TOOLS, pi_ssh, run

pytestmark = pytest.mark.pi


def connected_clients():
    """The agent's slots that are in use: logged-in clients and connections still logging in."""
    rc, out = pi_ssh("/data/opticnerve/bin/opticnerve status")
    assert rc == 0, out
    return [line for line in out.splitlines() if line.startswith("slot ") and " free " not in line]


def askpass_env(**extra):
    return dict(os.environ, SSH_ASKPASS=str(TOOLS / "pi_askpass.sh"), SSH_ASKPASS_REQUIRE="force", DISPLAY="x",
                **extra)


def test_ssh(target):
    assert target["name"]


def test_agent_status(target):
    rc, out = run([sys.executable, str(TOOLS / "opticnerve_deploy.py"), "status"])
    assert rc == 0 and "version" in out and "pid" in out, out


def test_frame_from_the_settings(target, tmp_path):
    from PIL import Image
    png = tmp_path / "frame.png"
    rc, out = run([sys.executable, str(TOOLS / "vnc_snapshot.py"), "-", "-", str(png)], timeout=60)
    assert rc == 0 and "auth: OK" in out, out
    rc, status = pi_ssh("/data/opticnerve/bin/opticnerve status")
    w, h = Image.open(png).size
    assert f"{w}x{h}" in status, f"frame {w}x{h}; agent: {status[:200]}"


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def test_ssh_through_a_port_forward(target):
    """Julien's case: SSH on another address and port than VNC. A forward 127.0.0.1:PORT -> the target's SSH."""
    import on_env
    port = free_port()
    fwd = subprocess.Popen(["ssh", "-N", "-o", "PubkeyAuthentication=no", "-o", "ExitOnForwardFailure=yes",
                            "-o", "StrictHostKeyChecking=accept-new", "-p", os.environ["ON_SSH_PORT"],
                            "-L", f"127.0.0.1:{port}:127.0.0.1:22",
                            f"{os.environ['ON_LOGIN_USER']}@{on_env.ssh_host()}"],
                           env=askpass_env(), stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                           stderr=subprocess.PIPE, text=True)
    try:
        for _ in range(100):   # up to 10 s for the forward to listen
            if fwd.poll() is not None:
                pytest.fail(f"the forward ended: {fwd.stderr.read()[-500:]}")
            try:
                socket.create_connection(("127.0.0.1", port), timeout=0.2).close()
                break
            except OSError:
                time.sleep(0.1)
        else:
            pytest.fail("the forward did not come up in 10 s")
        via = dict(os.environ, ON_SSH_HOST="127.0.0.1", ON_SSH_PORT=str(port))
        rc, out = run([SH, str(TOOLS / "pi_ssh.sh"), 'echo "$SSH_CONNECTION"'], env=via, timeout=60)
        assert rc == 0 and out.split()[:1] == ["127.0.0.1"], out   # it came in through the forward
        rc, out = run([sys.executable, str(TOOLS / "opticnerve_deploy.py"), "status"], env=via)
        assert rc == 0 and "version" in out, out
    finally:
        fwd.terminate()
        try:
            fwd.wait(10)
        except subprocess.TimeoutExpired:
            fwd.kill()
            fwd.wait(10)
        # pi_ssh.sh's accept-new recorded the target's key under this one-off port: take it out again
        subprocess.run(["ssh-keygen", "-R", f"[127.0.0.1]:{port}"], capture_output=True)
        old = os.path.expanduser("~/.ssh/known_hosts.old")
        if os.path.exists(old):
            os.remove(old)


@pytest.mark.slow
def test_core_test_on_the_target(target, agent_build):
    """The core's own test, built with the SDP here and run on the target, in a folder of its own."""
    import on_env
    user, work = os.environ["ON_LOGIN_USER"], f"ontest-{uuid.uuid4().hex[:12]}"
    rc, out = pi_ssh(f"mkdir {work}")   # not -p: a folder of that name must not exist already
    assert rc == 0, out
    try:
        r = subprocess.run(["scp", "-q", "-o", "PubkeyAuthentication=no", "-P", os.environ["ON_SSH_PORT"],
                            str(agent_build / "core_test"), f"{user}@{on_env.ssh_host()}:{work}/core_test"],
                           env=askpass_env(), capture_output=True, text=True, timeout=120)
        assert r.returncode == 0, r.stderr
        rc, out = pi_ssh(f"cd {work} && chmod 755 core_test && ./core_test", timeout=300)
        assert rc == 0, out[-2000:]
        assert " 0 failed" in out, out[-500:]
    finally:
        rc, out = pi_ssh(f"rm -r {work} && ! test -e {work}")
        assert rc == 0, f"could not remove {work} on the target: {out}"


@pytest.mark.slow
def test_robustness(target):
    if connected_clients():
        pytest.skip("a client is connected to the agent: the robustness checks fill every slot")
    rc, out = run([sys.executable, str(ROOT / "host/tests/robustness.py"), "-"], timeout=600)
    assert rc == 0, out[-3000:]


class Client:
    """A raw RFB client of the agent in its own pixel format (32 bpp, bytes B, G, R, 0), with a copy of the screen."""

    def __init__(self, target, encodings):
        import struct
        try:
            from cryptography.hazmat.decrepit.ciphers.algorithms import TripleDES
        except ImportError:  # cryptography < 43
            from cryptography.hazmat.primitives.ciphers.algorithms import TripleDES
        from cryptography.hazmat.primitives.ciphers import Cipher, modes

        def recvn(s, n):
            b = b""
            while len(b) < n:
                c = s.recv(n - len(b))
                if not c:
                    raise EOFError("closed")
                b += c
            return b

        def response(challenge):  # VncAuth: DES with each password byte's bits reversed
            pw = os.environ["ON_VNC_PASSWORD"].encode()
            key = bytes(int(f"{b:08b}"[::-1], 2) for b in pw.ljust(8, b"\0")[:8])
            enc = Cipher(TripleDES(key * 3), modes.ECB()).encryptor()
            return enc.update(challenge) + enc.finalize()

        self.struct, self.recvn = struct, recvn
        self.s = socket.create_connection((target["host"], target["vnc_port"]), timeout=10)
        recvn(self.s, 12)
        self.s.sendall(b"RFB 003.008\n")
        recvn(self.s, recvn(self.s, 1)[0])
        self.s.sendall(bytes([2]))
        self.s.sendall(response(recvn(self.s, 16)))
        assert struct.unpack(">I", recvn(self.s, 4))[0] == 0, "login refused"
        self.s.sendall(bytes([1]))
        self.w, self.h = struct.unpack(">HH", recvn(self.s, 4))
        recvn(self.s, 16)
        recvn(self.s, struct.unpack(">I", recvn(self.s, 4))[0])
        self.s.sendall(struct.pack(">BxH", 2, len(encodings)) + b"".join(struct.pack(">i", e) for e in encodings))
        self.fb = bytearray(self.w * self.h * 4)

    def request(self, incremental):
        self.s.sendall(self.struct.pack(">BBHHHH", 3, incremental, 0, 0, self.w, self.h))

    def update(self, timeout):
        """Reads one FramebufferUpdate into the copy; returns its rectangles, or None if none came in time."""
        self.s.settimeout(timeout)
        try:
            t = self.recvn(self.s, 1)[0]
        except socket.timeout:
            return None
        finally:
            self.s.settimeout(10)
        assert t == 0, f"message type {t}"
        n = self.struct.unpack(">xH", self.recvn(self.s, 3))[0]
        rects = []
        for _ in range(n):
            x, y, w, h, enc = self.struct.unpack(">HHHHi", self.recvn(self.s, 12))
            assert enc == 0, f"encoding {enc}"
            data = self.recvn(self.s, w * h * 4)
            for j in range(h):
                at = ((y + j) * self.w + x) * 4
                self.fb[at:at + w * 4] = data[j * w * 4:(j + 1) * w * 4]
            rects.append((x, y, w, h))
        return rects

    def pixel(self, x, y):
        at = (y * self.w + x) * 4
        return tuple(self.fb[at:at + 3])


ARROW = ["X           ", "XX          ", "X.X         ", "X..X        ", "X...X       ", "X....X      ",
         "X.....X     ", "X......X    ", "X.......X   ", "X........X  ", "X.........X ", "X......XXXXX",
         "X...X..X    ", "X..XX..X    ", "X.X  X..X   ", "XX   X..X   ", "X     X..X  ", "      X..X  ",
         "       XX   "]


def shows_arrow(c, x0, y0):
    """1 if the client's copy holds the arrow, every one of its pixels, with its tip at x0,y0."""
    for j, row in enumerate(ARROW):
        for i, k in enumerate(row):
            if k != " " and c.pixel(x0 + i, y0 + j) != ((255, 255, 255) if k == "." else (0, 0, 0)):
                return 0
    return 1


def agent(command, root=False):
    rc, out = pi_ssh(f"{'rs ' if root else ''}/data/opticnerve/bin/opticnerve {command}")
    assert rc == 0, out
    return out


def status_value(status, name):
    line = next((l for l in status.splitlines() if l.startswith(name + " ")), None)
    return line.split(" ", 1)[1] if line else None


def arrow_slots(status):
    slots = [l for l in status.splitlines() if l.startswith("slot ") and " client " in l]
    return len(slots), sum(" arrow" in l for l in slots)


@pytest.mark.watched
def test_pointer_arrow(target):
    """ON-AG-001: a viewer (asks for Cursor, -239) is sent the arrow where the pointer went, soon after it stops, also
    when its queue was full while the pointer moved; a test tool (asks for none) is not; `opticnerve pointer off`
    takes it away and `on` brings it back; the status shows which client gets it. Moves the pointer by hand, and
    (phase 2) holds the target's own mouse while the viewer is connected."""
    if connected_clients():
        pytest.skip("a client is connected to the agent: this test moves the target's pointer")
    if "pointer on" not in agent("status"):
        pytest.skip("the agent's pointer arrow is off (opticnerve pointer on), or the agent predates it")
    sys.path.insert(0, str(TOOLS))
    from hand import Hand
    viewer, tool = Client(target, [0, -239]), Client(target, [0])
    try:
        for c in (viewer, tool):
            c.request(0)
            assert c.update(10), "no first frame"
        assert arrow_slots(agent("status")) == (2, 1), "one of the two clients should get the arrow"
        hand = Hand(viewer.s.sendall, viewer.w, viewer.h, f"{target['host']}:{target['vnc_port']}")

        def far_from(x0, y0):  # a target well away from where the hand is, so a move is a real glide
            return (400, 300) if abs(x0 - 400) + abs(y0 - 300) > 300 else (1300, 700)

        # 1. a move, then requests: the arrow at the final place, soon
        x, y = far_from(*hand.pos)
        hand.move(x, y)
        stopped = time.time()
        seen = None
        while time.time() - stopped < 5 and seen is None:
            viewer.request(1)
            rects = viewer.update(1) or []
            if any(rx <= x < rx + rw and ry <= y < ry + rh for rx, ry, rw, rh in rects) and shows_arrow(viewer, x, y):
                seen = time.time() - stopped
        assert seen is not None, "the viewer was not sent the arrow at the place the pointer stopped"
        assert seen < 0.3, f"the arrow at its final place came {seen:.2f} s after the pointer stopped"

        # 2. a slow reader: a whole frame queued and an incremental request waiting while the pointer moves; once the
        #    frame is read, the arrow at the final place follows without another request (the agent decides again
        #    when a queue empties)
        viewer.request(0)
        time.sleep(0.5)  # the whole frame is captured and queued first (a request in the same pass would be used up by it)
        viewer.request(1)
        x, y = far_from(x, y)
        hand.move(x, y)
        assert viewer.update(10), "no whole frame"
        drained = time.time()
        rects = viewer.update(2) or []
        late = time.time() - drained
        assert any(rx <= x < rx + rw and ry <= y < ry + rh for rx, ry, rw, rh in rects) or shows_arrow(viewer, x, y), \
            f"after the queue drained, no update with the arrow at {x},{y} within 2 s"
        assert shows_arrow(viewer, x, y) and late < 0.3, f"the arrow came {late:.2f} s after the queue drained"

        # 3. the test tool's frame has no arrow
        tool.request(0)
        assert tool.update(10), "no frame for the test tool"
        assert not shows_arrow(tool, x, y), "the test tool's frame has the arrow"

        # 4. the switch: off takes the arrow back from the viewer, on brings it back
        assert agent("pointer off", root=True).strip() == "pointer off"
        assert "pointer off" in agent("status") and arrow_slots(agent("status")) == (2, 0)
        viewer.request(1)
        viewer.update(2)
        assert not shows_arrow(viewer, x, y), "switched off: the viewer still shows the arrow"
        assert agent("pointer on", root=True).strip() == "pointer on"
        viewer.request(1)
        viewer.update(2)
        assert shows_arrow(viewer, x, y) and arrow_slots(agent("status")) == (2, 1), "switched on: no arrow"
    finally:
        agent("pointer on", root=True)  # as the test found it (it runs only when the arrow was on)
        viewer.s.close()
        tool.s.close()


def test_switches_need_root(target):
    """Setting the pointer arrow or the mouse switch needs root (both change what a person at the target gets);
    showing them does not. Changes nothing: the requests made as the login user are refused."""
    status = agent("status")
    if status_value(status, "mouse") is None:
        pytest.skip("the agent predates the mouse switch")
    for name in ("pointer", "mouse"):
        before = status_value(status, name)
        assert agent(name).strip() == f"{name} {before}", name
        try:
            rc, out = pi_ssh(f"/data/opticnerve/bin/opticnerve {name} {'off' if before == 'on' else 'on'}")
            assert rc != 0 and "not permitted" in out, out
            assert status_value(agent("status"), name) == before, f"{name} changed although the request was refused"
        finally:
            agent(f"{name} {before}", root=True)  # as found, should the refusal have failed


def test_mouse_not_held_without_a_viewer(target):
    """Phase 2: with no client that gets the arrow, the agent does not hold the target's own mouse."""
    status = agent("status")
    if status_value(status, "mouse") is None:
        pytest.skip("the agent predates the mouse switch")
    if arrow_slots(status)[1]:
        pytest.skip("a viewer that gets the arrow is connected")
    assert status_value(status, "mouse_held") == "0", status


@pytest.mark.watched
def test_mouse_held_while_a_viewer_is_connected(target):
    """Phase 2: a viewer that gets the arrow (asks for Cursor) makes the agent take the target's own mouse after about
    1 s; a test tool (asks for none) does not; `mouse off` gives it back with the viewer still there; the viewer
    leaving gives it back. Takes the mouse from the person at the target for a few seconds."""
    if connected_clients():
        pytest.skip("a client is connected to the agent")
    status = agent("status")
    if status_value(status, "mouse") != "on" or "pointer on" not in status:
        pytest.skip("the mouse switch or the pointer arrow is off, or the agent predates them")

    def held_within(want, seconds):
        end = time.time() + seconds
        while time.time() < end:
            if status_value(agent("status"), "mouse_held") == want:
                return True
            time.sleep(0.2)
        return False

    refused_before = status_value(status, "mouse_attach_failures")
    tool = Client(target, [0])
    viewer = None
    try:
        time.sleep(1.5)
        assert status_value(agent("status"), "mouse_held") == "0", "a test tool alone made the agent take the mouse"
        viewer = Client(target, [0, -239])
        assert held_within("1", 3), "a viewer that gets the arrow: the mouse was not taken within 3 s"
        st = agent("status")
        assert status_value(st, "mouse_attach_failures") == refused_before, f"an attach was refused:\n{st}"
        if status_value(st, "mouse_devices") == "0":
            import warnings
            warnings.warn("no mouse is plugged in to the target: the hold had nothing to hold")
        assert agent("mouse off", root=True).strip() == "mouse off"
        assert held_within("0", 2), "mouse off: still held"
        assert agent("mouse on", root=True).strip() == "mouse on"
        assert held_within("1", 3), "mouse on again with the viewer there: not taken"
        viewer.s.close()
        viewer = None
        assert held_within("0", 7), "the viewer left: the mouse was not given back"
    finally:
        agent("mouse on", root=True)  # as the test found it (it runs only when the switch was on)
        if viewer:
            viewer.s.close()
        tool.s.close()


def test_demo_panel_start_and_stop(target):
    panel = [SH, str(TOOLS / "demo_panel.sh")]
    if connected_clients():
        pytest.skip("a client is connected to the agent: someone may be looking at the screen")
    rc, out = run(panel + ["status"])
    if "demo panel running" in out:
        pytest.skip("a demo panel is running: someone may be using it")
    try:
        rc, out = run(panel + ["start", "60"], timeout=60)
        assert rc == 0 and "demo panel started" in out, out
        rc, out = run(panel + ["status"])
        assert "demo panel running" in out, out
    finally:
        rc, out = run(panel + ["stop"], timeout=60)
    assert rc == 0 and "demo panel stopped" in out, out
