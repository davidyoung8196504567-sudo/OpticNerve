"""The target's tests (marker `pi`, run with --pi): SSH, the agent, a frame, SSH through a port forward (SSH on another
address and port than VNC), the core's own test on the target, the robustness checks, the demo panel, the pointer
arrow and the launcher panel.

    python -m pytest --pi -m pi

None of them moves the target's pointer or types, except the pointer arrow test, the mouse hold test, the demo
panel's close X test and the launcher panel test, which are marked `watched` and run only with --watched as well (one
at a time, watched, when agreed). The first two connect a viewer that gets the arrow, so they take the target's own mouse from the person at
it for a few seconds:

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


PANELS_WITHOUT_X = {   # demo_panel builds for aarch64le from before ON-AG-002 (golden/agent_build.json)
    "02b51069d163c06305f39199522a61c2e64d3977300096db0098a11462d71ba2",
    "f2ded66d37dfbe89347e6d62e6b73c848afe55fe6bca7a9029983ba508fb211c"}


@pytest.mark.watched
def test_demo_panel_close_x(target):
    """ON-AG-002: the X at the top right of the panel's title bar closes it, as in a windowed GUI. A press there turns
    the box red; sliding off takes the red away and a release there leaves the panel open; a click closes it. Moves
    and clicks the target's pointer."""
    panel = [SH, str(TOOLS / "demo_panel.sh")]
    if connected_clients():
        pytest.skip("a client is connected to the agent: someone may be looking at the screen")
    rc, out = run(panel + ["status"])
    if "demo panel running" in out:
        pytest.skip("a demo panel is running: someone may be using it")
    sys.path.insert(0, str(TOOLS))
    from hand import Hand
    title, red, white = (0x62, 0x3d, 0x0a), (0x23, 0x11, 0xe8), (255, 255, 255)   # B, G, R as the client keeps them
    rc, out = pi_ssh("(sha256sum demo_panel 2>/dev/null || openssl dgst -sha256 -r demo_panel) | cut -d' ' -f1")
    assert rc == 0, out
    if out.strip() in PANELS_WITHOUT_X:
        pytest.skip(f"the panel on the target is a build from before the close X ({out.strip()[:8]})")
    tool = None
    try:
        rc, out = run(panel + ["start", "120"], timeout=60)
        assert rc == 0 and "demo panel started" in out, out
        tool = Client(target, [0])   # a test tool: no arrow drawn into its frames, and the target's mouse not taken

        def frame():
            tool.request(0)
            assert tool.update(10), "no frame"

        def running():
            return "demo panel running" in run(panel + ["status"])[1]

        frame()
        bx, by = tool.w - 55, 55     # the middle of the X, where its two strokes cross
        assert (tool.pixel(bx, by), tool.pixel(tool.w - 100, 10)) == (white, title),             f"no close X: {tool.pixel(bx, by)} at the X, {tool.pixel(tool.w - 100, 10)} in its box"
        hand = Hand(tool.s.sendall, tool.w, tool.h, f"{target['host']}:{target['vnc_port']}")

        # 1. pressed: the box turns red; slid off: no longer red; released there: the panel stays
        hand.move(bx, by)
        hand.buttons = 1
        hand._pointer(bx, by)
        time.sleep(0.5)
        frame()
        assert tool.pixel(tool.w - 100, 10) == red, "pressed: the close box is not red"
        hand.move(tool.w - 400, 300)
        time.sleep(0.5)
        frame()
        assert tool.pixel(tool.w - 100, 10) == title, "slid off with the button held: the close box is still red"
        hand.buttons = 0
        hand._pointer(*hand.pos)
        time.sleep(1)
        assert running(), "a press on the X released outside it closed the panel"
        frame()
        assert tool.pixel(tool.w - 100, 10) == title, "released outside: the close box is red"

        # 2. a click on the X: the panel closes
        hand.click(bx, by)
        end = time.time() + 5
        while running() and time.time() < end:
            time.sleep(0.5)
        assert not running(), "a click on the X did not close the panel within 5 s"
        assert "closed from the panel" in pi_ssh("cat demo.log")[1]
    finally:
        if tool:
            tool.s.close()
        run(panel + ["stop"], timeout=60)


LAUNCH = "/data/opticnerve/bin/launch_panel"
LAUNCH_TILES = ["Developer Desktop", "Terminal", "Readme", "GTK4Demo", "Gears", "Maze", "Widget Factory"]


def launch_panel_installed():
    rc, out = pi_ssh(f"test -x {LAUNCH} && echo yes")
    return "yes" in out


def test_launch_panel_reads_the_configuration(target):
    """ON-AG-003, LP02 (the panel's configuration reader): the launcher panel's tiles from a configuration (`launch_panel -list`, no screen): the image's own,
    then a prepared one with the cases its reader must handle. Changes nothing on the screen."""
    if not launch_panel_installed():
        pytest.skip("no launcher panel on the target (install the agent with opticnerve_deploy.py install)")
    rc, out = pi_ssh(f"{LAUNCH} -list")
    assert rc == 0, out
    names = [l.split("|")[0].split(" ", 2)[2] for l in out.splitlines() if "|" in l]
    assert names == LAUNCH_TILES, out
    work = f"ontest-lp-{uuid.uuid4().hex[:12]}"
    long_line = "Comment=" + "x" * 503 + "Exec=/bin/evil"   # 511 bytes, then a key that must not be read
    files = {
        "desktop.conf": "# a comment\nLogoText=x\n[Launcher]\nDesktopFileName=b.desktop\nRow=2\nOrder=1\n"
                        "[Launcher]\n DesktopFileName = a.desktop \r\nRow=1\nOrder=2\n"
                        "[Launcher]\nDesktopFileName=a2.desktop\nRow=1\nOrder=1\n"
                        "[Launcher]\nDesktopFileName=a3.desktop\nRow=1\nOrder=1\n"
                        "[Launcher]\nDesktopFileName=long.desktop\n[Launcher]\nDesktopFileName=missing.desktop\n"
                        "[Launcher]\nDesktopFileName=../x.desktop\n[Launcher]\nDesktopFileName=noexec.desktop\n"
                        "[Other]\nDesktopFileName=b.desktop\n",
        "a.desktop": f"[Desktop Entry]\nName=Alpha\nName[de]=Alfa\nExec=/bin/alpha %U\n{long_line}\nIcon=/i/a.png\n"
                     "[Desktop Action x]\nExec=/bin/wrong\n",
        "a2.desktop": "[Desktop Entry]\nName=Beta\nExec=echo 50%%\n",
        "a3.desktop": "[Desktop Entry]\nName=Gamma\nExec=/bin/gamma\n",
        "b.desktop": "[Desktop Entry]\nName=Delta\nExec=GDK_BACKEND=qnx /bin/delta\n",
        "long.desktop": "[Desktop Entry]\nName=Long\nExec=/bin/" + "y" * 300 + "\n",
        "noexec.desktop": "[Desktop Entry]\nName=NoExec\n",
    }
    make = " && ".join(f"printf '%s' '{c}' > {work}/{n}" for n, c in files.items())
    rc, out = pi_ssh(f"mkdir {work} && {make}")
    assert rc == 0, out
    try:
        rc, out = pi_ssh(f"{LAUNCH} -list -noextra -conf {work}")
        assert rc == 0, out
        assert [l for l in out.splitlines() if "|" in l] == [
            "1 1 Beta|echo 50%|", "1 1 Gamma|/bin/gamma|", "1 2 Alpha|/bin/alpha|/i/a.png",
            "2 1 Delta|GDK_BACKEND=qnx /bin/delta|"], out
        for why in ("long.desktop: Exec over 255 bytes", "missing.desktop", "../x.desktop is not a file name",
                    "noexec.desktop: no Name or no Exec", "a line of 511 bytes or more"):
            assert why in out, f"{why!r} not reported:\n{out}"
    finally:
        rc, out = pi_ssh(f"rm -r {work} && ! test -e {work}")
        assert rc == 0, f"could not remove {work} on the target: {out}"


def hand_on(target, c):
    """A hand (host/tools/hand.py) that sends on the client c: the one way tests send input."""
    sys.path.insert(0, str(TOOLS))
    from hand import Hand
    return Hand(c.s.sendall, c.w, c.h, f"{target['host']}:{target['vnc_port']}")


def press_in_turn(h, steps):
    """Sends (keysym, down, wait after) steps with the hand h; whatever it pressed and did not release is released,
    also when a step fails or the run is interrupted."""
    down = []
    try:
        for sym, d, wait in steps:
            h.key(sym, d)
            if d:
                down.append(sym)
            elif sym in down:
                down.remove(sym)
            time.sleep(wait)
    finally:
        for sym in reversed(down):
            h.key(sym, False)


def apps_running(*names):
    """The pids of the target's processes whose name (as pidin shows it, e.g. usr/bin/st) is one of names."""
    rows = (l.split() for l in pi_ssh("rs pidin -f an")[1].splitlines())
    return [r[0] for r in rows if len(r) >= 2 and r[1] in names]


def close_selected_app(target, looks_like, frames):
    """Alt+Tab+Del from a test tool, aimed: hold Alt, press Tab one at a time and, with Tab still down, take a frame
    (frames/tabN.png); the window switcher shows the selected window in the middle of its strip; press Del only when
    that thumbnail looks_like(mean_rgb) the app, else go on, at most 6 Tabs. Returns (Del sent, the means seen)."""
    from PIL import Image
    alt, tab, dele = 0xffe9, 0xff09, 0xffff
    c = Client(target, [0])
    h = hand_on(target, c)

    def key(sym, down):
        h.key(sym, bool(down))
        time.sleep(0.15)
    sent, seen, tab_down = False, [], False
    frames.mkdir(parents=True, exist_ok=True)
    try:
        key(alt, 1)
        for n in range(1, 7):
            key(tab, 1)
            tab_down = True
            time.sleep(0.6)
            path = frames / f"tab{n}.png"
            rc, out = run([sys.executable, str(TOOLS / "vnc_snapshot.py"), "-", "-", str(path)], timeout=60)
            assert rc == 0, out
            box = Image.open(path).convert("RGB").crop((800, 475, 1110, 605)).resize((32, 16))
            px = [box.getpixel((x, y)) for y in range(box.height) for x in range(box.width)]
            rgb = tuple(round(sum(p[k] for p in px) / len(px)) for k in range(3))
            seen.append(rgb)
            if looks_like(rgb):
                key(dele, 1)
                key(dele, 0)
                time.sleep(0.6)
                sent = True
            key(tab, 0)
            tab_down = False
            if sent:
                break
    finally:
        try:
            if tab_down:
                key(tab, 0)
            key(alt, 0)
        finally:
            time.sleep(1.5)
            c.s.close()
    return sent, seen


PANEL_TITLE = (0x62, 0x3d, 0x0a)   # B, G, R of the launcher panel's title bar, as the client keeps them


def launch_panel_in_front(target):
    c = Client(target, [0])
    c.request(0)
    assert c.update(10), "no frame"
    front = c.pixel(100, 30) == PANEL_TITLE
    c.s.close()
    return front


def launch_tile_centres():
    """Where the launcher panel's tiles are, from its log ("tile NAME at X,Y size WxH"; the last start's win)."""
    where = {}
    for l in pi_ssh("rs cat /data/opticnerve/var/launch.log")[1].splitlines():
        if l.startswith("tile ") and " at " in l:
            name, rest = l[5:].rsplit(" at ", 1)
            xy, size = rest.split(" size ")
            (x, y), (w, h) = map(int, xy.split(",")), map(int, size.split("x"))
            where[name] = (x + w // 2, y + h // 2)
    return where


def launch_panel_skips(target):
    """The reasons a watched launcher panel test does not run now, as pytest.skip does."""
    if connected_clients():
        pytest.skip("a client is connected to the agent: someone may be looking at the screen")
    if not launch_panel_installed():
        pytest.skip("no launcher panel on the target (install the agent with opticnerve_deploy.py install)")
    for script, name in (("demo_panel.sh", "demo panel"), ("launch_panel.sh", "launcher panel")):
        if f"{name} running" in run([SH, str(TOOLS / script), "status"])[1]:
            pytest.skip(f"a {name} is running: someone may be using it")
    if apps_running("usr/bin/st", "usr/bin/gles3-gears"):
        pytest.skip("a Terminal or Gears is open on the target")
    probe = Client(target, [0])
    probe.s.close()
    if (probe.w, probe.h) != (1920, 1080):
        pytest.skip(f"the switcher and close X positions are those of 1920x1080, not {probe.w}x{probe.h}")


def tool_click(target, xy):
    tool = Client(target, [0])
    hand_on(target, tool).click(*xy)
    time.sleep(0.3)   # the release is injected before the connection goes
    tool.s.close()


@pytest.mark.watched
def test_launch_panel(target, tmp_path):
    """ON-AG-003, LP03 (each tile opens its app as often as needed), LP06 (running apps listed, never blocking) and
    LP07 (stop and the close X): the launcher panel, driven by a test tool as an OculiX test drives it. A click on the
    Terminal tile opens a Terminal, Alt+Tab+Del (aimed at it) closes it and the panel is in front again, and the next
    click opens it again; the same for Gears; while an app runs the panel lists it, and when it has closed no more; a
    click on a tile whose app runs starts a second one; `stop` leaves a started app running and `stop -apps` ends it;
    the close X ends the panel. Moves and clicks the target's pointer and types (1920x1080 only)."""
    launch_panel_skips(target)
    panel = [SH, str(TOOLS / "launch_panel.sh")]
    apps = {"Terminal": ("usr/bin/st", lambda rgb: max(rgb) < 25),                                 # a black window
            "Gears": ("usr/bin/gles3-gears", lambda rgb: rgb[2] > 150 and rgb[2] > 2 * max(rgb[:2]))}   # mostly blue

    def click(xy):
        tool_click(target, xy)

    def panel_in_front():
        return launch_panel_in_front(target)

    try:
        rc, out = run(panel + ["start"], timeout=60)
        assert rc == 0 and "launcher panel started" in out, out
        where = launch_tile_centres()
        assert set(LAUNCH_TILES) <= set(where), where
        assert panel_in_front(), "the launcher panel is not on the screen"
        for name, (proc, looks_like) in apps.items():
            for n in (1, 2):   # opens it as often as needed
                click(where[name])
                end = time.time() + 10
                while not apps_running(proc) and time.time() < end:
                    time.sleep(0.5)
                assert apps_running(proc), f"{name}, click {n}: the app did not start within 10 s"
                time.sleep(2)
                status = run(panel + ["status"])[1]
                assert f"started by it: {name}" in status, status
                sent, seen = close_selected_app(target, looks_like, tmp_path / f"{name}-{n}")
                assert sent, f"{name}, click {n}: never selected in the window switcher (middle means {seen})"
                end = time.time() + 10
                while apps_running(proc) and time.time() < end:
                    time.sleep(0.5)
                assert not apps_running(proc), f"{name}, click {n}: still running after Alt+Tab+Del"
                assert panel_in_front(), f"{name}, click {n}: the panel is not in front after the app closed"
                time.sleep(1)   # the panel looks at its apps every 0.5 s
                status = run(panel + ["status"])[1]
                assert f"started by it: {name}" not in status, f"{name}, click {n}: still listed\n{status}"
                time.sleep(3)   # past the panel's guard against a second start within 3 s

        # a running tile never blocks: Gears, then (past the guard, with Alt+Tab back to the panel) Gears again
        click(where["Gears"])
        time.sleep(4)
        assert len(apps_running("usr/bin/gles3-gears")) == 1, "Gears did not start"
        c = Client(target, [0])   # Alt+Tab once: the window behind Gears, the panel, comes to the front
        try:
            press_in_turn(hand_on(target, c), ((0xffe9, True, 0.2), (0xff09, True, 0.2), (0xff09, False, 0.2),
                                               (0xffe9, False, 1.5)))
        finally:
            c.s.close()
        assert panel_in_front(), "Alt+Tab from Gears did not bring the panel to the front"
        click(where["Gears"])
        time.sleep(4)
        assert len(apps_running("usr/bin/gles3-gears")) == 2, "a click on a running tile did not start a second app"
        assert run(panel + ["status"])[1].count("started by it: Gears") == 2

        # stop leaves the apps running; stop -apps ends them
        rc, out = run(panel + ["stop"], timeout=60)
        assert rc == 0 and "launcher panel stopped" in out, out
        assert len(apps_running("usr/bin/gles3-gears")) == 2, "stopping the panel ended the apps it started"
        rc, out = run(panel + ["stop", "-apps"], timeout=60)
        assert out.count("ended Gears") == 2 and "still runs" not in out, out
        assert not apps_running("usr/bin/gles3-gears"), out

        # the close X ends the panel
        rc, out = run(panel + ["start"], timeout=60)
        assert rc == 0, out
        click((1920 - 55, 55))
        time.sleep(2)
        assert "no launcher panel" in run(panel + ["status"])[1]
        assert "closed from the panel" in pi_ssh("rs tail -3 /data/opticnerve/var/launch.log")[1]
    finally:
        run(panel + ["stop", "-apps"], timeout=60)


def chord_close_selected_app(target, looks_like, frames):
    """The key chord as a viewer sends it, aimed (ON-AG-004): hold Q 0.4 s, hold N (its auto-repeat, sent by hand as
    a second N down after 0.5 s, opens the switcher), take a frame, and if the middle thumbnail looks_like(mean_rgb)
    the app press X; else let N go and press it again (one step each), at most 6. Q up ends the chord. Returns (X
    sent, the means seen)."""
    from PIL import Image
    c = Client(target, [0])
    h = hand_on(target, c)
    strip = (300, 475, 1620, 605)   # the switcher's strip of thumbnails, at 1920x1080

    def key(ch, down):
        h.key(ord(ch), bool(down))
        time.sleep(0.15)

    def frame(path):
        rc, out = run([sys.executable, str(TOOLS / "vnc_snapshot.py"), "-", "-", str(path)], timeout=60)
        assert rc == 0, out
        return Image.open(path).convert("RGB")

    def mean(img, box):
        small = img.crop(box).resize((32, 16))
        px = [small.getpixel((x, y)) for y in range(small.height) for x in range(small.width)]
        return tuple(round(sum(p[k] for p in px) / len(px)) for k in range(3))
    sent, seen, n_down, q_down = False, [], False, False
    frames.mkdir(parents=True, exist_ok=True)
    before = mean(frame(frames / "before.png"), strip)
    try:
        key("q", 1)
        q_down = True
        time.sleep(0.4)
        for n in range(1, 7):
            key("n", 1)
            n_down = True
            if n == 1:
                time.sleep(0.5)
                key("n", 1)   # the repeat of a held key: the chord begins with N still down
            time.sleep(0.6)
            img = frame(frames / f"n{n}.png")
            rgb, open_ = mean(img, (800, 475, 1110, 605)), mean(img, strip) != before
            seen.append((rgb, "switcher" if open_ else "no switcher"))
            if open_ and looks_like(rgb):
                key("x", 1)
                key("x", 0)
                time.sleep(0.6)
                sent = True
            key("n", 0)
            n_down = False
            if sent:
                break
    finally:
        try:
            if n_down:
                key("n", 0)
            if q_down:
                key("q", 0)
        finally:
            time.sleep(1.5)
            c.s.close()
    return sent, seen


def type_text(target, text, rollover):
    """Types text and Return through the agent, as a person: with rollover, each key goes down 40 ms before the key
    before it goes up (then the hand's gap to the next); without, each key goes down and up in turn."""
    c = Client(target, [0])
    h = hand_on(target, c)
    try:
        if not rollover:
            h.keys([ord(ch) for ch in text] + [0xff0d])
        else:
            prev = None
            for sym in [ord(ch) for ch in text] + [0xff0d]:
                if prev == sym:
                    h.key(prev, False)
                    prev = None
                h.key(sym, True)
                time.sleep(0.04)
                if prev is not None:
                    h.key(prev, False)
                prev = sym
            h.key(prev, False)
        time.sleep(0.5)
    finally:
        c.s.close()


@pytest.mark.watched
def test_key_chord(target, tmp_path):
    """ON-AG-004, KC01 (typing is unchanged with the chord on) and KC02 (the chord switches and closes apps): with the
    launcher panel up and a Terminal opened from it, a command with q, n and x typed with rollover and without reaches
    the Terminal as typed; Q held with N tapped brings the panel to the front and again the Terminal (Alt+Tab); Q held,
    N held and X (aimed at the Terminal with the switcher's frame) closes it and the panel is in front. Moves and
    clicks the target's pointer and types (1920x1080 only)."""
    launch_panel_skips(target)
    status = agent("status")
    if status_value(status, "chord") != "on":
        pytest.skip(f"the agent's key chord is not on (chord {status_value(status, 'chord')!r})")
    chords = int(status_value(status, "chords"))
    panel = [SH, str(TOOLS / "launch_panel.sh")]
    out_file = "/data/home/qnxuser/on_chord.txt"

    def chord_tap():
        """Q held 0.4 s, N tapped, Q let go: one Alt+Tab. The connection's slot shows no chord state after it."""
        c = Client(target, [0])
        try:
            press_in_turn(hand_on(target, c), ((ord("q"), True, 0.4), (ord("n"), True, 0.1), (ord("n"), False, 0.6),
                                               (ord("q"), False, 1.5)))
            slots = [l for l in agent("status").splitlines() if l.startswith("slot ")]
            assert slots and not any(" chord_" in l for l in slots), f"a slot still in a chord: {slots}"
        finally:
            c.s.close()

    try:
        pi_ssh(f"rs rm -f {out_file}")
        rc, out = run(panel + ["start"], timeout=60)
        assert rc == 0 and "launcher panel started" in out, out
        where = launch_tile_centres()
        assert "Terminal" in where, where
        tool_click(target, where["Terminal"])
        end = time.time() + 10
        while not apps_running("usr/bin/st") and time.time() < end:
            time.sleep(0.5)
        assert apps_running("usr/bin/st"), "the Terminal did not start within 10 s"
        time.sleep(2)

        # KC01: typing, with and without rollover
        type_text(target, f"echo qnxuser qnx >> {out_file}", rollover=True)
        type_text(target, f"echo qnx quiet next >> {out_file}", rollover=False)
        # Q held back, then N 0.2 s later with Q let go first (a roll after the hold), and N 0.05 s later (too soon)
        c = Client(target, [0])
        try:
            h = hand_on(target, c)
            h.keys([ord(ch) for ch in "echo "])
            time.sleep(0.3)
            q, n = ord("q"), ord("n")
            press_in_turn(h, ((q, True, 0.2), (n, True, 0.04), (q, False, 0.04), (n, False, 0.3)))
            h.key(ord("x"))
            time.sleep(0.3)
            press_in_turn(h, ((q, True, 0.05), (n, True, 0.04), (n, False, 0.04), (q, False, 0.3)))
            h.keys([ord(ch) for ch in f" >> {out_file}"] + [0xff0d])
            time.sleep(0.5)
        finally:
            c.s.close()
        time.sleep(1)
        rc, got = pi_ssh(f"cat {out_file}")
        assert rc == 0 and got.splitlines() == ["qnxuser qnx", "qnx quiet next", "qnxqn"], \
            f"typed text arrived as {got!r}"

        # KC02: Q+N switches: the panel comes to the front, then the Terminal again
        chord_tap()
        assert launch_panel_in_front(target), "Q held with N tapped did not bring the panel to the front"
        chord_tap()
        assert not launch_panel_in_front(target), "Q held with N tapped did not bring the Terminal back"

        # KC02: Q+N+X closes the app selected
        sent, seen = chord_close_selected_app(target, lambda rgb: max(rgb) < 25, tmp_path / "close")
        assert sent, f"the Terminal was never selected in the window switcher (middle means {seen})"
        end = time.time() + 10
        while apps_running("usr/bin/st") and time.time() < end:
            time.sleep(0.5)
        assert not apps_running("usr/bin/st"), "the Terminal still runs after Q+N+X"
        assert launch_panel_in_front(target), "the panel is not in front after the Terminal closed"
        status = agent("status")
        assert int(status_value(status, "chords")) == chords + 3, status
    finally:
        run(panel + ["stop", "-apps"], timeout=60)
        pi_ssh(f"rs rm -f {out_file}")
