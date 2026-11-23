"""Shared helpers for the OpticNerve regression tests (tests/regression/README.md).

The host tests run every script in a clean environment that holds only what the test gives it and a settings file of
the test's own (ON_CONFIG), so this host's real settings and passwords never decide a result. The target's tests
(marker `pi`) use the real settings, as a person running the scripts would.
"""
from __future__ import annotations

import os
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "host" / "tools"
sys.path.insert(0, str(TOOLS))


def find_sh():
    """A POSIX sh: /bin/sh (dash on Ubuntu) on Linux, Git's sh on Windows (not WSL's bash in System32)."""
    if os.name != "nt":
        return "/bin/sh"
    for c in (shutil.which("sh"), r"C:\Program Files\Git\usr\bin\sh.exe", r"C:\Program Files\Git\bin\sh.exe"):
        if c and Path(c).is_file() and "system32" not in c.lower():
            return c
    return None


SH = find_sh()
JAVA = shutil.which("java")


def clean_env(**extra):
    """The environment of a fresh login: PATH, HOME and what Windows needs to start programs, plus EXTRA."""
    env = {k: os.environ[k] for k in ("PATH", "HOME", "SYSTEMROOT", "WINDIR", "TEMP", "TMP", "USERPROFILE",
                                      "APPDATA", "LOCALAPPDATA", "PATHEXT", "COMSPEC") if k in os.environ}
    env.setdefault("HOME", str(Path.home()))
    env.update({k: str(v) for k, v in extra.items()})
    return env


def run(args, env=None, timeout=120, cwd=ROOT, input=None):
    """Runs ARGS and returns (exit status, stdout + stderr)."""
    r = subprocess.run(args, env=env, cwd=cwd, capture_output=True, text=True, timeout=timeout, input=input,
                       encoding="utf-8", errors="replace")
    return r.returncode, r.stdout + r.stderr


@pytest.fixture
def no_settings(tmp_path):
    """A clean environment whose settings file does not exist: only the built-in defaults apply."""
    return clean_env(ON_CONFIG=tmp_path / "none.env")


@pytest.fixture
def need_sh():
    if not SH:
        pytest.skip("no POSIX sh on this host")
    return SH


@pytest.fixture(scope="session")
def agent_build(tmp_path_factory):
    """The agent built for aarch64le in a scratch copy of agent/; the folder holding the binaries."""
    sdp = Path(os.environ.get("QNX_SDP_DIR") or Path.home() / "qnx805")
    if not (sdp / "qnxsdp-env.sh").is_file() or not SH:
        pytest.skip(f"no QNX SDP at {sdp} (set QNX_SDP_DIR)")
    work = tmp_path_factory.mktemp("agent")
    shutil.copytree(ROOT / "agent", work / "agent", ignore=shutil.ignore_patterns("build"))
    env = dict(os.environ, QNX_SDP_DIR=str(sdp), WORK=str(work / "agent"))
    # bash on Linux: the SDP's qnxsdp-env.sh does not set PATH under dash; Git's sh on Windows (bash itself)
    shell = SH if os.name == "nt" else (shutil.which("bash") or SH)
    r = subprocess.run([shell, "-c", '. "$QNX_SDP_DIR/qnxsdp-env.sh" > /dev/null && make -C "$WORK" ARCH=aarch64le'],
                       env=env, capture_output=True, text=True, timeout=600)
    assert r.returncode == 0, (r.stdout + r.stderr)[-3000:]
    return work / "agent" / "build" / "aarch64le"


# The target's tests: the real settings, and a skip (not a failure) when the target is not set up from here.
@pytest.fixture(scope="session")
def target():
    import on_env
    on_env.load()
    missing = [v for v in ("ON_LOGIN_PASSWORD", "ON_VNC_PASSWORD") if not os.environ.get(v)]
    if missing:
        pytest.skip(f"set {', '.join(missing)} (environment or settings file) for the target's tests")
    if not SH:
        pytest.skip("no POSIX sh on this host")
    rc, out = run([SH, str(TOOLS / "pi_ssh.sh"), "uname -n"], timeout=30)
    if rc:
        pytest.skip(f"the target does not answer over SSH ({on_env.ssh_host()}:{os.environ['ON_SSH_PORT']}): {out[-200:]}")
    return {"host": os.environ["ON_HOST"], "vnc_port": int(os.environ["ON_VNC_PORT"]), "name": out.strip()}


def pi_ssh(commands, timeout=120):
    return run([SH, str(TOOLS / "pi_ssh.sh"), commands], timeout=timeout)
