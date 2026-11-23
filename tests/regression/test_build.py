"""Everything in the repository still parses and builds: the shell scripts (sh -n), the Python (compile), the Java
tests (javac, against the OculiX jar), and the agent with the QNX SDP. The agent is built in a scratch copy of agent/,
so a test run never touches agent/build/.

The build is also checked against tests/regression/golden/agent_build.json: for a recorded state of agent/ (git's
tree hash), the SHA-256 of each binary. The same sources give the same bytes on Windows and on Ubuntu with the
baseline SDP (README), so a different hash means a different SDP or a changed build. Record a new state with
`python tests/regression/record_build.py` after a reviewed agent change.
"""
import hashlib
import json
import os
import py_compile
import shutil
import subprocess
from pathlib import Path

import pytest

from conftest import ROOT, SH, run

GOLDEN = Path(__file__).parent / "golden" / "agent_build.json"
BINARIES = ["opticnerve", "demo_panel", "core_test"]


def tracked(*patterns):
    out = subprocess.run(["git", "-C", str(ROOT), "ls-files", *patterns], capture_output=True, text=True).stdout
    return [ROOT / p for p in out.split()]


def test_shell_scripts_parse(need_sh):
    bad = []
    for f in tracked("*.sh"):
        rc, out = run([need_sh, "-n", str(f)])
        if rc:
            bad.append(f"{f.relative_to(ROOT)}: {out.strip()}")
    assert bad == []


def test_shell_scripts_are_lf():
    assert [str(f.relative_to(ROOT)) for f in tracked("*.sh") if b"\r" in f.read_bytes()] == []


def test_python_compiles(tmp_path):
    bad = []
    for f in tracked("*.py"):
        if ".sikuli" in f.parts[-2]:   # OculiX IDE scripts are Jython 2.7, checked by test_settings_readers
            continue
        try:
            py_compile.compile(str(f), cfile=str(tmp_path / "x.pyc"), doraise=True)
        except py_compile.PyCompileError as e:
            bad.append(str(e))
    assert bad == []


def oculix_classpath():
    """The OculiX jar (and TigerVNC jar, if set): ON_OCULIX_JAR / ON_TIGERVNC_JAR, from the environment or settings."""
    import on_env
    on_env.load()
    jars = [os.environ.get(v) for v in ("ON_TIGERVNC_JAR", "ON_OCULIX_JAR")]
    jars = [j for j in jars if j and Path(j).is_file()]
    if not any("oculix" in Path(j).name for j in jars):
        pytest.skip("set ON_OCULIX_JAR to an OculiX jar to compile the Java tests")
    return os.pathsep.join(jars)


@pytest.mark.slow
def test_java_tests_compile(tmp_path):
    javac = shutil.which("javac")
    if not javac:
        pytest.skip("no javac on PATH")
    sources = [str(f) for f in tracked("host/demo/*.java")]
    rc, out = run([javac, "-nowarn", "-cp", oculix_classpath(), "-d", str(tmp_path), *sources], timeout=300)
    assert rc == 0, out


def sha256(f):
    return hashlib.sha256(Path(f).read_bytes()).hexdigest()


def agent_tree():
    """git's hash of agent/ as committed, or None when the working copy of agent/ differs from it."""
    if subprocess.run(["git", "-C", str(ROOT), "diff", "--quiet", "HEAD", "--", "agent"]).returncode:
        return None
    return subprocess.run(["git", "-C", str(ROOT), "rev-parse", "HEAD:agent"], capture_output=True,
                          text=True).stdout.strip()


@pytest.mark.slow
def test_agent_builds(agent_build):
    for b in BINARIES:
        assert (agent_build / b).is_file(), b   # -Werror: a warning would have failed the build


@pytest.mark.slow
def test_agent_build_matches_the_recorded_bytes(agent_build):
    tree = agent_tree()
    if tree is None:
        pytest.skip("agent/ has uncommitted changes: nothing recorded to compare with")
    golden = json.loads(GOLDEN.read_text())
    if tree not in golden:
        pytest.skip(f"no recorded build for agent/ tree {tree[:12]}: run tests/regression/record_build.py")
    want = golden[tree]["aarch64le"]
    assert {b: sha256(agent_build / b) for b in BINARIES} == want
