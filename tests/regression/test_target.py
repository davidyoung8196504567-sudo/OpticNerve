"""The target's tests (marker `pi`): SSH, the agent, a frame, SSH through a port forward (SSH on another address
and port than VNC), the core's own test on the target, the robustness checks and the demo panel.

    python -m pytest -m pi

None of them moves the target's pointer or types. They leave the target as they found it: the robustness checks
and the demo panel test skip, rather than disturb, when a viewer is connected or a panel is running. The core test
copies core_test to /data/home/<user>/ontest/ and deletes the folder afterwards.
"""
import os
import socket
import subprocess
import sys
import time
from pathlib import Path

import pytest

from conftest import ROOT, SH, TOOLS, pi_ssh, run

pytestmark = pytest.mark.pi


def logged_in_clients():
    rc, out = pi_ssh("/data/opticnerve/bin/opticnerve status")
    assert rc == 0, out
    return [line for line in out.splitlines() if line.startswith("slot ") and " client " in line]


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
    env = dict(os.environ, SSH_ASKPASS=str(TOOLS / "pi_askpass.sh"), SSH_ASKPASS_REQUIRE="force", DISPLAY="x")
    fwd = subprocess.Popen(["ssh", "-N", "-o", "PubkeyAuthentication=no", "-o", "ExitOnForwardFailure=yes",
                            "-o", "StrictHostKeyChecking=accept-new", "-p", os.environ["ON_SSH_PORT"],
                            "-L", f"127.0.0.1:{port}:127.0.0.1:22",
                            f"{os.environ['ON_LOGIN_USER']}@{on_env.ssh_host()}"],
                           env=env, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    try:
        for _ in range(100):   # up to 10 s for the forward to listen
            try:
                socket.create_connection(("127.0.0.1", port), timeout=0.2).close()
                break
            except OSError:
                time.sleep(0.1)
        else:
            pytest.fail("the forward did not come up")
        via = dict(os.environ, ON_SSH_HOST="127.0.0.1", ON_SSH_PORT=str(port))
        rc, out = run([SH, str(TOOLS / "pi_ssh.sh"), 'echo "$SSH_CONNECTION"'], env=via, timeout=60)
        assert rc == 0 and out.split()[:1] == ["127.0.0.1"], out   # it came in through the forward
        rc, out = run([sys.executable, str(TOOLS / "opticnerve_deploy.py"), "status"], env=via)
        assert rc == 0 and "version" in out, out
    finally:
        fwd.terminate()
        fwd.wait(10)


@pytest.mark.slow
def test_core_test_on_the_target(target, agent_build):
    """The core's own test, built with the SDP here and run on the target."""
    user = os.environ["ON_LOGIN_USER"]
    import on_env
    env = dict(os.environ, SSH_ASKPASS=str(TOOLS / "pi_askpass.sh"), SSH_ASKPASS_REQUIRE="force", DISPLAY="x")
    rc, out = pi_ssh("mkdir -p ontest")
    assert rc == 0, out
    try:
        r = subprocess.run(["scp", "-q", "-o", "PubkeyAuthentication=no", "-P", os.environ["ON_SSH_PORT"],
                            str(agent_build / "core_test"), f"{user}@{on_env.ssh_host()}:ontest/core_test"],
                           env=env, capture_output=True, text=True, timeout=120)
        assert r.returncode == 0, r.stderr
        rc, out = pi_ssh("cd ontest && chmod 755 core_test && ./core_test", timeout=300)
        assert rc == 0, out[-2000:]
        assert " 0 failed" in out, out[-500:]
    finally:
        pi_ssh("rm -rf ontest")


@pytest.mark.slow
def test_robustness(target):
    if logged_in_clients():
        pytest.skip("a client (a viewer) is connected: the robustness checks fill every slot")
    rc, out = run([sys.executable, str(ROOT / "host/tests/robustness.py"), "-"], timeout=600)
    assert rc == 0, out[-3000:]


def test_demo_panel_start_and_stop(target):
    panel = [SH, str(TOOLS / "demo_panel.sh")]
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
