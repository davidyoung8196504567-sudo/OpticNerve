#!/usr/bin/env python3
"""Records the SHA-256 of the agent's aarch64le binaries for the committed state of agent/, in
tests/regression/golden/agent_build.json, which test_build.py compares every build with.

    python tests/regression/record_build.py

Builds in a scratch copy with the QNX SDP (QNX_SDP_DIR, default ~/qnx805), which must match the baseline: the
script runs sdp/check_sdp.py first. Run it after a reviewed change to agent/ is committed, not to make a failing
comparison pass: a hash that changed without a change to agent/ is a finding (a different SDP or build).
"""
import hashlib, json, os, shutil, subprocess, sys, tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
GOLDEN = Path(__file__).parent / "golden" / "agent_build.json"
BINARIES = ["opticnerve", "demo_panel", "core_test"]


def main():
    git = lambda *a: subprocess.run(["git", "-C", str(ROOT), *a], capture_output=True, text=True)
    if git("status", "--porcelain", "--untracked-files=all", "--", "agent").stdout.strip():
        sys.exit("agent/ has uncommitted or untracked files: commit or remove them first")
    # the tree of agent/, and the commit that last changed it
    tree, commit = git("rev-parse", "HEAD:agent").stdout.strip(), git("log", "-1", "--format=%h", "--", "agent").stdout.strip()
    sdp = Path(os.environ.get("QNX_SDP_DIR") or Path.home() / "qnx805")
    if subprocess.run([sys.executable, str(ROOT / "sdp" / "check_sdp.py"), str(sdp)]).returncode:
        sys.exit("the SDP does not match the baseline: not recording")
    # bash on Linux (the SDP's qnxsdp-env.sh needs it), Git's sh on Windows (not WSL's bash in System32)
    sh = shutil.which("bash") if os.name != "nt" else (shutil.which("sh") or r"C:\Program Files\Git\usr\bin\sh.exe")
    with tempfile.TemporaryDirectory() as work:
        shutil.copytree(ROOT / "agent", Path(work) / "agent", ignore=shutil.ignore_patterns("build"))
        env = dict(os.environ, QNX_SDP_DIR=str(sdp), WORK=str(Path(work) / "agent"))
        if os.name == "nt":   # Git's tools first, also when run from PowerShell or cmd
            env["PATH"] = str(Path(sh).parent) + os.pathsep + env["PATH"]
        subprocess.run([sh, "-c", '. "$QNX_SDP_DIR/qnxsdp-env.sh" > /dev/null && make -C "$WORK" ARCH=aarch64le'],
                       env=env, check=True)
        out = Path(work) / "agent" / "build" / "aarch64le"
        hashes = {b: hashlib.sha256((out / b).read_bytes()).hexdigest() for b in BINARIES}
    golden = json.loads(GOLDEN.read_text()) if GOLDEN.is_file() else {}
    if tree in golden and golden[tree]["aarch64le"] != hashes:
        # the same sources built to other bytes: a finding to look into, not something to record over
        old = golden[tree]["aarch64le"]
        lines = [f"  {b}  recorded {old.get(b)}\n  {' ' * len(b)}  built    {h}" for b, h in hashes.items()]
        sys.exit(f"agent/ tree {tree[:12]} is already recorded with other bytes; nothing changed:\n" + "\n".join(lines)
                 + "\nIf the recorded bytes are known to be wrong, delete that entry from the file by hand, with the"
                 " reason in the commit message.")
    golden[tree] = {"commit": commit, "aarch64le": hashes}
    GOLDEN.parent.mkdir(exist_ok=True)
    GOLDEN.write_text(json.dumps(golden, indent=2) + "\n", newline="\n")
    print(f"recorded agent/ tree {tree[:12]} (commit {commit}):")
    for b, h in hashes.items():
        print(f"  {b}  {h}")


if __name__ == "__main__":
    main()
