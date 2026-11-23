"""The target's tests (marker `pi`, run with --pi): SSH, the agent, a frame, SSH through a port forward (SSH on another
address and port than VNC), the core's own test on the target, the robustness checks and the demo panel.

    python -m pytest --pi -m pi

None of them moves the target's pointer or types. They leave the target as they found it: the robustness checks and
the demo panel test skip, rather than disturb, while any client is connected to the agent (also one still at its
password prompt) or a panel is running. The core test works in a folder of its own on the target and deletes it.
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
