#!/usr/bin/env python3
"""Install, start, stop and remove the OpticNerve agent on the QNX target, and record each step.

    python host/tools/opticnerve_deploy.py install [BINARY]   copy the agent to /data/opticnerve/ and set its password
    python host/tools/opticnerve_deploy.py start              start `opticnerve serve` as root (port 5901)
    python host/tools/opticnerve_deploy.py status             print the agent's status text
    python host/tools/opticnerve_deploy.py stop               stop the agent (opticnerve stop, as root)
    python host/tools/opticnerve_deploy.py undo               stop the agent and delete /data/opticnerve/

BINARY defaults to agent/build/aarch64le/opticnerve. Everything on the target lives in one folder,
/data/opticnerve/ (bin/, etc/, var/); nothing else on the target is changed, and `vncserv` is not touched.
Each step prints what it changed and the SHA-256 of each file it put there, for the record in
docs/CONFIG_MANAGEMENT.md.

Environment: ON_LOGIN_PASSWORD (the target's qnxuser password, also used for sudo), ON_VNC_PASSWORD (the password
our server will ask for), optionally ON_HOST (default 10.30.1.116) and ON_LOGIN_USER (default qnxuser). Passwords
go to the target on SSH's standard input only, never on a command line. Needs bash, ssh and scp on PATH.
"""
import hashlib, os, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "host" / "tools"
DEST = "/data/opticnerve"
HOST = os.environ.get("ON_HOST", "10.30.1.116")
USER = os.environ.get("ON_LOGIN_USER", "qnxuser")
STAGE = f"/data/home/{USER}/.opticnerve-stage"   # not /tmp: the image tidies /tmp


def remote(script, check=True):
    """Runs a script on the target through pi_ssh.sh; `rs CMD` runs CMD as root. Returns its output."""
    r = subprocess.run(["bash", str(TOOLS / "pi_ssh.sh"), script], capture_output=True, text=True, timeout=120)
    out = (r.stdout + r.stderr).strip()
    if check and r.returncode:
        sys.exit(f"target step failed (exit {r.returncode}):\n{out}")
    return out


def copy(local, remote_path):
    env = dict(os.environ, SSH_ASKPASS=str(TOOLS / "pi_askpass.sh"), SSH_ASKPASS_REQUIRE="force", DISPLAY="x")
    r = subprocess.run(["scp", "-q", "-o", "PubkeyAuthentication=no", "-o", "ConnectTimeout=10",
                        str(local), f"{USER}@{HOST}:{remote_path}"], env=env, capture_output=True, text=True, timeout=120)
    if r.returncode:
        sys.exit(f"copy failed: {r.stderr.strip()}")


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def install(binary):
    binary = Path(binary)
    if not binary.is_file():
        sys.exit(f"no binary at {binary}: build it with make in agent/")
    pw = os.environ.get("ON_VNC_PASSWORD")
    if not pw or "'" in pw or "\n" in pw:
        sys.exit("set ON_VNC_PASSWORD (without quotes or newlines)")
    want = sha256(binary)
    remote(f"mkdir -p {STAGE} && chmod 700 {STAGE}")
    copy(binary, f"{STAGE}/opticnerve")
    out = remote(f"""
        set -e
        rs mkdir -p {DEST}/bin {DEST}/etc {DEST}/var
        rs cp {STAGE}/opticnerve {DEST}/bin/opticnerve
        rs chown root:root {DEST}/bin/opticnerve
        rs chmod 755 {DEST}/bin/opticnerve
        umask 077
        printf '%s\\n' '{pw}' > {STAGE}/pw
        rs sh -c '{DEST}/bin/opticnerve passwd -passfile={DEST}/etc/vncpasswd < {STAGE}/pw'
        rm -rf {STAGE}
        rs chmod 600 {DEST}/etc/vncpasswd
        sha256sum {DEST}/bin/opticnerve
        ls -l {DEST}/bin/opticnerve {DEST}/etc/vncpasswd 2>&1 || rs ls -l {DEST}/etc/vncpasswd
    """)
    got = out.split("\n")[0].split()[0] if out else ""
    if got != want:
        sys.exit(f"installed file differs: want {want}, target has {got!r}\n{out}")
    print(f"installed {DEST}/bin/opticnerve (root, 755), SHA-256 {want}")
    print(f"password file {DEST}/etc/vncpasswd (root, 600); its content is not recorded")
    print(f"undo: python host/tools/opticnerve_deploy.py undo")


def start():
    out = remote(f"""
        test -x {DEST}/bin/opticnerve || {{ echo 'not installed'; exit 1; }}
        rs sh -c '{DEST}/bin/opticnerve serve -passfile={DEST}/etc/vncpasswd < /dev/null > /dev/null 2>> {DEST}/var/serve.log'
        sleep 1
        {DEST}/bin/opticnerve status | head -4
    """)
    print(out)


def status():
    print(remote(f"{DEST}/bin/opticnerve status", check=False))


def stop():
    out = remote(f"""
        if test -e /dev/opticnerve; then rs {DEST}/bin/opticnerve stop && echo stopped; else echo 'not running'; fi
        rs tail -3 {DEST}/var/serve.log 2>/dev/null
    """, check=False)
    print(out)


def undo():
    stop()
    out = remote(f"""
        rs slay -f opticnerve 2>/dev/null
        rs rm -rf {DEST} {STAGE}
        ls -d {DEST} 2>&1
        test -e /dev/opticnerve && echo 'device still there' || echo 'device gone'
        echo "core dumps: $(rs ls /data/var/dumper | wc -l)"
        rs pidin -p vncserv -f an | tail -1
    """, check=False)
    print(out)
    print(f"removed {DEST}; vncserv line above should show the image's own vncserv, unchanged")


def main():
    if len(sys.argv) < 2 or sys.argv[1] not in ("install", "start", "status", "stop", "undo"):
        sys.exit(__doc__)
    if not os.environ.get("ON_LOGIN_PASSWORD"):
        sys.exit("set ON_LOGIN_PASSWORD")
    cmd = sys.argv[1]
    if cmd == "install":
        install(sys.argv[2] if len(sys.argv) > 2 else ROOT / "agent" / "build" / "aarch64le" / "opticnerve")
    else:
        {"start": start, "status": status, "stop": stop, "undo": undo}[cmd]()


if __name__ == "__main__":
    main()
