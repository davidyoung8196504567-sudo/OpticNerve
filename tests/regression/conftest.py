"""Shared helpers for the OpticNerve regression tests (tests/regression/README.md).

The host tests run every script in a sandbox: an environment that holds only what the test gives it, a home folder
of their own, a settings file of their own (ON_CONFIG), and stand-in `ssh`, `scp` and `notify-send` first on PATH
that only print what they were asked. So this host's settings and passwords never decide a result, nothing is
written to the real home, and a script that should have refused but didn't cannot reach the target.

The target's tests (marker `pi`) run only with --pi, and use the real settings, as a person running the scripts would.
"""
from __future__ import annotations

import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "host" / "tools"
sys.path.insert(0, str(TOOLS))


def find_sh():
    """A POSIX sh: /bin/sh (dash on Ubuntu) on Linux and macOS, Git's sh on Windows (not WSL's bash in System32)."""
    if os.name != "nt":
        return "/bin/sh"
    for c in (shutil.which("sh"), r"C:\Program Files\Git\usr\bin\sh.exe", r"C:\Program Files\Git\bin\sh.exe"):
        if c and Path(c).is_file() and "system32" not in c.lower():
            return c
    return None


SH = find_sh()
if os.name == "nt" and SH:
    # Git's tools (tr, sed, grep, ssh, scp...) first on PATH, also when pytest runs from PowerShell or cmd, whose PATH
    # has only Git\cmd: without them the scripts fall back silently, and Windows' own ssh cannot run pi_askpass.sh.
    os.environ["PATH"] = str(Path(SH).parent) + os.pathsep + os.environ["PATH"]


def find_java(tool="java"):
    """java or javac: on PATH, else next to ON_JAVA from the environment, else the PoC tester's JDK 17."""
    found = shutil.which(tool)
    if found:
        return found
    for j in (os.environ.get("ON_JAVA"), str(Path.home() / "opticnerve-work/jdk/jdk17/bin/java")):
        if j and (Path(j).parent / tool).is_file():
            return str(Path(j).parent / tool)
    return None


JAVA = find_java()

# The sandbox: a home folder and the stand-in commands, one per test session
SANDBOX = Path(tempfile.mkdtemp(prefix="on-regression-"))
HOME = SANDBOX / "home"
STUBS = SANDBOX / "bin"
HOME.mkdir()
STUBS.mkdir()
for name in ("ssh", "scp", "notify-send"):
    f = STUBS / name
    f.write_text('#!/bin/sh\necho "' + name + ' $*"\n', newline="\n")
    f.chmod(0o755)


def clean_env(**extra):
    """The sandbox environment: the stand-ins first on PATH, the sandbox home, a display that does not exist, and
    only what Windows needs to start programs and find Python's packages; plus EXTRA."""
    env = {k: os.environ[k] for k in ("PATH", "SYSTEMROOT", "WINDIR", "TEMP", "TMP", "APPDATA", "LOCALAPPDATA",
                                      "PATHEXT", "COMSPEC") if k in os.environ}
    env["PATH"] = str(STUBS) + os.pathsep + env["PATH"]
    env.update(HOME=str(HOME), USERPROFILE=str(HOME), DISPLAY=":99")
    env.update({k: str(v) for k, v in extra.items()})
    return env


def run(args, env=None, timeout=120, cwd=ROOT, input=None):
    """Runs ARGS and returns (exit status, stdout + stderr)."""
    r = subprocess.run(args, env=env, cwd=cwd, capture_output=True, text=True, timeout=timeout, input=input,
                       encoding="utf-8", errors="replace")
    return r.returncode, r.stdout + r.stderr


@pytest.fixture
def no_settings(tmp_path):
    """A sandbox whose settings file does not exist: only the built-in defaults apply."""
    return clean_env(ON_CONFIG=tmp_path / "none.env")


@pytest.fixture
def safe_target(tmp_path):
    """A sandbox whose settings point every address at a closed port on 127.0.0.1, for tests of scripts that must
    refuse before they connect: if one ever does connect, it reaches nothing."""
    f = tmp_path / "safe.env"
    f.write_text("ON_HOST='127.0.0.1'\nON_VNC_PORT='1'\nON_SSH_PORT='1'\n")
    return clean_env(ON_CONFIG=f)


@pytest.fixture
def need_sh():
    if not SH:
        pytest.skip("no POSIX sh on this host")
    return SH


@pytest.fixture(scope="session")
def agent_build(tmp_path_factory):
    """The agent built for aarch64le in a scratch copy of agent/; the folder holding the binaries. On a host without
    the QNX SDP (macOS), the prebuilt binaries in ON_AGENT_BUILD instead, if they are a recorded build."""
    sdp = Path(os.environ.get("QNX_SDP_DIR") or Path.home() / "qnx805")
    if not (sdp / "qnxsdp-env.sh").is_file() or not SH:
        import hashlib, json, on_env
        on_env.load()
        pre = os.environ.get("ON_AGENT_BUILD")
        if not pre:
            pytest.skip(f"no QNX SDP at {sdp} (set QNX_SDP_DIR) and no prebuilt agent (ON_AGENT_BUILD)")
        names = ("opticnerve", "demo_panel", "core_test")
        got = {n: hashlib.sha256((Path(pre) / n).read_bytes()).hexdigest() for n in names}
        golden = json.loads((Path(__file__).parent / "golden" / "agent_build.json").read_text())
        assert any(e["aarch64le"] == got for e in golden.values()), f"{pre} is not a recorded build: {got}"
        return Path(pre)
    work = tmp_path_factory.mktemp("agent")
    shutil.copytree(ROOT / "agent", work / "agent", ignore=shutil.ignore_patterns("build"))
    env = dict(os.environ, QNX_SDP_DIR=str(sdp), WORK=str(work / "agent"))
    # bash on Linux: the SDP's qnxsdp-env.sh does not set PATH under dash; Git's sh on Windows (bash itself)
    shell = SH if os.name == "nt" else (shutil.which("bash") or SH)
    r = subprocess.run([shell, "-c", '. "$QNX_SDP_DIR/qnxsdp-env.sh" > /dev/null && make -C "$WORK" ARCH=aarch64le'],
                       env=env, capture_output=True, text=True, timeout=600)
    assert r.returncode == 0, (r.stdout + r.stderr)[-3000:]
    return work / "agent" / "build" / "aarch64le"


# The target's tests: only with --pi (a -m expression cannot turn them on by accident), and then a target that cannot
# be reached is a failure, not a skip: whoever asked for them is checking the target.
def pytest_addoption(parser):
    parser.addoption("--pi", action="store_true", help="also run the target's tests (marker pi): the Pi at ON_HOST")


def pytest_collection_modifyitems(config, items):
    if config.getoption("--pi"):
        return
    target = [i for i in items if i.get_closest_marker("pi")]
    if target:
        config.hook.pytest_deselected(items=target)
        items[:] = [i for i in items if not i.get_closest_marker("pi")]


@pytest.fixture(scope="session")
def target():
    import on_env
    on_env.load()
    missing = [v for v in ("ON_LOGIN_PASSWORD", "ON_VNC_PASSWORD") if not os.environ.get(v)]
    if missing:
        pytest.fail(f"set {', '.join(missing)} (environment or settings file) for the target's tests")
    if not SH:
        pytest.fail("no POSIX sh on this host")
    rc, out = run([SH, str(TOOLS / "pi_ssh.sh"), "uname -n"], timeout=30)
    if rc:
        pytest.fail(f"the target does not answer over SSH ({on_env.ssh_host()}:{os.environ['ON_SSH_PORT']}): {out[-200:]}")
    return {"host": os.environ["ON_HOST"], "vnc_port": int(os.environ["ON_VNC_PORT"]), "name": out.strip()}


def pi_ssh(commands, timeout=120):
    return run([SH, str(TOOLS / "pi_ssh.sh"), commands], timeout=timeout)


def pytest_sessionfinish(session, exitstatus):
    shutil.rmtree(SANDBOX, ignore_errors=True)
