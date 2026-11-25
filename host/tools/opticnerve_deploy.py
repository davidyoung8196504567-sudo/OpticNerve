#!/usr/bin/env python3
"""Install, start, stop and remove the OpticNerve agent on the QNX target, and record each step.

    python host/tools/opticnerve_deploy.py install [BINARY]   copy the agent to /data/opticnerve/ and set its password
                                                              (and the launcher panel, launch_panel, when BINARY's
                                                              folder has one)
    python host/tools/opticnerve_deploy.py start [OPTION...]  start `opticnerve serve` as root (port 5901), with
                                                              serve's options, e.g. -logintimeout=0 (no login limit)
    python host/tools/opticnerve_deploy.py status             print the agent's status text
    python host/tools/opticnerve_deploy.py pointer [on|off]   show or switch the pointer arrow drawn for viewers
    python host/tools/opticnerve_deploy.py mouse [on|off]     show or switch whether the agent may hold the target's
                                                              own mouse while a viewer that gets the arrow is connected
    python host/tools/opticnerve_deploy.py stop               stop the agent (opticnerve stop, as root)
    python host/tools/opticnerve_deploy.py undo               stop the agent and delete /data/opticnerve/

BINARY defaults to agent/build/aarch64le/opticnerve. Everything on the target lives in one folder,
/data/opticnerve/ (bin/, etc/, var/); nothing else on the target is changed, and `vncserv` is not touched.
Each step prints what it changed and the SHA-256 of each file it put there, for the record in
docs/CONFIG_MANAGEMENT.md.

Settings, from the environment or the settings file (host/tools/on_env.py): ON_LOGIN_PASSWORD (the target's login
password, also used for sudo), ON_VNC_PASSWORD (the password our server will ask for), ON_SSH_HOST (default ON_HOST,
10.30.1.116), ON_SSH_PORT (default 22), ON_LOGIN_USER (default qnxuser), ON_AGENT_POINTER (0: `start` starts the
agent with the pointer arrow off, -nopointer; default 1, on), ON_AGENT_MOUSE (0: `start` starts it with the mouse
switch off, -nomouse; default 1, on). Switching either on a running agent needs root, so `pointer` and `mouse` run
it with sudo. Passwords go to the target on SSH's standard input only, never on a command line. Needs bash, ssh and scp on PATH.
"""
import hashlib, os, re, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "host" / "tools"
sys.path.insert(0, str(TOOLS))
import on_env
on_env.load()
DEST = "/data/opticnerve"
HOST = on_env.ssh_host()
PORT = os.environ["ON_SSH_PORT"]
USER = os.environ["ON_LOGIN_USER"]
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
    r = subprocess.run(["scp", "-q", "-o", "PubkeyAuthentication=no", "-o", "ConnectTimeout=10", "-P", PORT,
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
    panel = binary.parent / "launch_panel"   # the launcher panel, from the same build, when there is one
    remote(f"mkdir -p {STAGE} && chmod 700 {STAGE}")
    copy(binary, f"{STAGE}/opticnerve")
    if panel.is_file():
        copy(panel, f"{STAGE}/launch_panel")
    PANEL = f"""rs slay -f launch_panel 2>/dev/null || true   # a running launcher panel is replaced, not copied over
        rs cp {STAGE}/launch_panel {DEST}/bin/launch_panel
        rs chown root:root {DEST}/bin/launch_panel
        rs chmod 755 {DEST}/bin/launch_panel""" if panel.is_file() else ""
    out = remote(f"""
        set -e
        rs mkdir -p {DEST}/bin {DEST}/etc {DEST}/var
        rs cp {STAGE}/opticnerve {DEST}/bin/opticnerve
        rs chown root:root {DEST}/bin/opticnerve
        rs chmod 755 {DEST}/bin/opticnerve
        {PANEL}
        umask 077
        printf '%s\\n' '{pw}' > {STAGE}/pw
        rs sh -c '{DEST}/bin/opticnerve passwd -passfile={DEST}/etc/vncpasswd < {STAGE}/pw'
        rm -rf {STAGE}
        rs chmod 600 {DEST}/etc/vncpasswd
        sha256sum {DEST}/bin/opticnerve 2>/dev/null || openssl dgst -sha256 -r {DEST}/bin/opticnerve   # QEMU image: no sha256sum
        ls -l {DEST}/bin/opticnerve {DEST}/etc/vncpasswd 2>&1 || rs ls -l {DEST}/etc/vncpasswd
    """)
    got = out.split("\n")[0].split()[0] if out else ""
    if got != want:
        sys.exit(f"installed file differs: want {want}, target has {got!r}\n{out}")
    print(f"installed {DEST}/bin/opticnerve (root, 755), SHA-256 {want}")
    if panel.is_file():
        p_want = sha256(panel)
        p_got = remote(f"sha256sum {DEST}/bin/launch_panel 2>/dev/null || openssl dgst -sha256 -r {DEST}/bin/launch_panel")
        p_got = p_got.split()[0] if p_got else ""
        if p_got != p_want:
            sys.exit(f"installed launch_panel differs: want {p_want}, target has {p_got!r}")
        print(f"installed {DEST}/bin/launch_panel (root, 755), SHA-256 {p_want}")
    print(f"password file {DEST}/etc/vncpasswd (root, 600); its content is not recorded")
    print(f"undo: python host/tools/opticnerve_deploy.py undo")


def start(options):
    for o in options:   # serve's own options only; -foreground would block this step, -passfile is set here
        if not re.fullmatch(r"-(display|port|holdoff|logintimeout)=[0-9]{1,6}|-v|-nopointer|-nomouse|-nochord", o):
            sys.exit(f"not a serve option: {o!r} (-display=N -port=P -holdoff=MS -logintimeout=S -nopointer "
                     f"-nomouse -nochord -v)")
    for name, opt in (("ON_AGENT_POINTER", "-nopointer"), ("ON_AGENT_MOUSE", "-nomouse")):
        value = os.environ.get(name) or "1"   # empty means unset, as in on_env
        if value not in ("0", "1"):
            sys.exit(f"{name} must be 0 or 1, not {value!r}")
        if value == "0" and opt not in options:
            options = list(options) + [opt]
    opts = "".join(" " + o for o in options)
    print(f"serve -passfile={DEST}/etc/vncpasswd{opts}")
    out = remote(f"""
        test -x {DEST}/bin/opticnerve || {{ echo 'not installed'; exit 1; }}
        rs sh -c '{DEST}/bin/opticnerve serve -passfile={DEST}/etc/vncpasswd{opts} < /dev/null > /dev/null 2>> {DEST}/var/serve.log'
        sleep 1
        {DEST}/bin/opticnerve status | head -4
    """)
    print(out)


def status():
    print(remote(f"{DEST}/bin/opticnerve status", check=False))


def switch(name, args):
    if args not in ([], ["on"], ["off"]):
        sys.exit(f"usage: opticnerve_deploy.py {name} [on|off]")
    print(remote(f"rs {DEST}/bin/opticnerve {name} {' '.join(args)}"))


def stop():
    out = remote(f"""
        if test -e /dev/opticnerve; then rs {DEST}/bin/opticnerve stop && echo stopped; else echo 'not running'; fi
        rs tail -3 {DEST}/var/serve.log 2>/dev/null
    """, check=False)
    print(out)


def undo():
    stop()
    if (TOOLS / "launch_panel.sh").is_file():   # the launcher panel and the apps it started
        try:
            r = subprocess.run(["bash", str(TOOLS / "launch_panel.sh"), "stop", "-apps"], capture_output=True,
                               text=True, timeout=120)
            print((r.stdout + r.stderr).strip())
        except (OSError, subprocess.TimeoutExpired) as e:
            print(f"launcher panel not stopped: {e}")
    out = remote(f"""
        rs slay -f opticnerve 2>/dev/null
        rs slay -f launch_panel 2>/dev/null
        rs rm -rf {DEST} {STAGE}
        ls -d {DEST} 2>&1
        test -e /dev/opticnerve && echo 'device still there' || echo 'device gone'
        echo "core dumps: $(rs ls /data/var/dumper | wc -l)"
        rs pidin -p vncserv -f an | tail -1
    """, check=False)
    print(out)
    print(f"removed {DEST}; vncserv line above should show the image's own vncserv, unchanged")


def main():
    if len(sys.argv) < 2 or sys.argv[1] not in ("install", "start", "status", "pointer", "mouse", "stop", "undo"):
        sys.exit(__doc__)
    if not os.environ.get("ON_LOGIN_PASSWORD"):
        sys.exit("set ON_LOGIN_PASSWORD")
    cmd = sys.argv[1]
    if cmd == "install":
        install(sys.argv[2] if len(sys.argv) > 2 else ROOT / "agent" / "build" / "aarch64le" / "opticnerve")
    elif cmd == "start":
        start(sys.argv[2:])
    elif cmd in ("pointer", "mouse"):
        switch(cmd, sys.argv[2:])
    else:
        {"status": status, "stop": stop, "undo": undo}[cmd]()


if __name__ == "__main__":
    main()
