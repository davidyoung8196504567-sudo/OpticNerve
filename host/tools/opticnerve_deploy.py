#!/usr/bin/env python3
"""Install, start, stop and remove the OpticNerve agent on the QNX target, and record each step.

    python host/tools/opticnerve_deploy.py install [BINARY]   copy the agent to /data/opticnerve/ and set its password
                                                              (and the launcher panel, launch_panel, when BINARY's
                                                              folder has one, with the OpticNerve mark in share/)
    python host/tools/opticnerve_deploy.py start [OPTION...]  start `opticnerve serve` as root (port 5901), with
                                                              serve's options, e.g. -logintimeout=0 (no login limit)
    python host/tools/opticnerve_deploy.py boot on [OPTION...] start the agent at boot, with serve's options as for
                                                              start: adds two lines to the image's
                                                              /system/etc/startup/post_startup.sh (a backup kept as
                                                              post_startup.sh.orig-opticnerve) and writes
                                                              /data/opticnerve/etc/boot.sh; starts nothing now
    python host/tools/opticnerve_deploy.py boot off           no start at boot: takes the two lines out, deletes boot.sh
    python host/tools/opticnerve_deploy.py boot               say whether the agent starts at boot, and how
    python host/tools/opticnerve_deploy.py status             print the agent's status text
    python host/tools/opticnerve_deploy.py pointer [on|off]   show or switch the pointer arrow drawn for viewers
    python host/tools/opticnerve_deploy.py mouse [on|off]     show or switch whether the agent may hold the target's
                                                              own mouse while a viewer that gets the arrow is connected
    python host/tools/opticnerve_deploy.py stop               stop the agent (opticnerve stop, as root)
    python host/tools/opticnerve_deploy.py undo               stop the agent, boot off, and delete /data/opticnerve/

BINARY defaults to agent/build/aarch64le/opticnerve. Everything on the target lives in one folder,
/data/opticnerve/ (bin/, etc/, var/, share/); nothing else on the target is changed (with `boot on`: two lines in
the image's post_startup.sh, and its backup), and `vncserv` is not touched.
Each step prints what it changed and the SHA-256 of each file it put there, for the record in
docs/CONFIG_MANAGEMENT.md.

Settings, from the environment or the settings file (host/tools/on_env.py): ON_LOGIN_PASSWORD (the target's login
password, also used for sudo), ON_VNC_PASSWORD (the password our server will ask for), ON_SSH_HOST (default ON_HOST,
10.30.1.116), ON_SSH_PORT (default 22), ON_LOGIN_USER (default qnxuser), ON_AGENT_POINTER (0: `start` starts the
agent with the pointer arrow off, -nopointer; default 1, on), ON_AGENT_MOUSE (0: `start` starts it with the mouse
switch off, -nomouse; default 1, on). Switching either on a running agent needs root, so `pointer` and `mouse` run
it with sudo. Passwords go to the target on SSH's standard input only, never on a command line. Needs bash, ssh and scp on PATH.
"""
import hashlib, os, re, subprocess, sys, tempfile
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
    icon = ROOT / "agent" / "demo" / "opticnerve_mark.png"   # its title bar's mark and demo panel tile's icon
    if panel.is_file() and not icon.is_file():
        sys.exit(f"no icon at {icon}")
    remote(f"mkdir -p {STAGE} && chmod 700 {STAGE}")
    copy(binary, f"{STAGE}/opticnerve")
    if panel.is_file():
        copy(panel, f"{STAGE}/launch_panel")
        copy(icon, f"{STAGE}/opticnerve_mark.png")
    PANEL = f"""rs slay -f launch_panel 2>/dev/null || true   # a running launcher panel is replaced, not copied over
        rs cp {STAGE}/launch_panel {DEST}/bin/launch_panel
        rs chown root:root {DEST}/bin/launch_panel
        rs chmod 755 {DEST}/bin/launch_panel
        rs mkdir -p {DEST}/share
        rs cp {STAGE}/opticnerve_mark.png {DEST}/share/opticnerve_mark.png
        rs chown root:root {DEST}/share/opticnerve_mark.png
        rs chmod 644 {DEST}/share/opticnerve_mark.png""" if panel.is_file() else ""
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
        i_want = sha256(icon)
        i_got = remote(f"sha256sum {DEST}/share/opticnerve_mark.png 2>/dev/null || "
                       f"openssl dgst -sha256 -r {DEST}/share/opticnerve_mark.png")
        i_got = i_got.split()[0] if i_got else ""
        if i_got != i_want:
            sys.exit(f"installed opticnerve_mark.png differs: want {i_want}, target has {i_got!r}")
        print(f"installed {DEST}/share/opticnerve_mark.png (root, 644), SHA-256 {i_want}")
    print(f"password file {DEST}/etc/vncpasswd (root, 600); its content is not recorded")
    print(f"undo: python host/tools/opticnerve_deploy.py undo")


def serve_options(options):
    """serve's options from the command line and ON_AGENT_POINTER / ON_AGENT_MOUSE, as " -a -b" (checked)."""
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
    return "".join(" " + o for o in options)


def start(options):
    opts = serve_options(options)
    print(f"serve -passfile={DEST}/etc/vncpasswd{opts}")
    out = remote(f"""
        test -x {DEST}/bin/opticnerve || {{ echo 'not installed'; exit 1; }}
        rs sh -c '{DEST}/bin/opticnerve serve -passfile={DEST}/etc/vncpasswd{opts} < /dev/null > /dev/null 2>> {DEST}/var/serve.log'
        sleep 1
        {DEST}/bin/opticnerve status | head -4
    """)
    print(out)


# Start at boot: the image starts its services from POST as root, after Screen is up. `boot on` adds HOOK's two lines
# before its last step (a backup kept as POST_BACKUP) and writes BOOT, which starts the agent with the options given;
# `boot off` takes the two lines out again (marked by HOOK_MARK) and deletes BOOT. While /data/opticnerve is gone the
# line does nothing.
POST = "/system/etc/startup/post_startup.sh"
POST_BACKUP = f"{POST}.orig-opticnerve"
BOOT = f"{DEST}/etc/boot.sh"
SCREEN_SH = f"{DEST}/etc/screen.sh"   # run by BOOT first when present (host/qemu/guest_boot.sh puts one there)
HOOK_MARK = "# OpticNerve agent at boot"
HOOK = f"{HOOK_MARK} (host/tools/opticnerve_deploy.py boot on|off)\\n[ -x {BOOT} ] && {BOOT} &\\n"


def boot_state():
    return remote(f"""
        echo "hook lines in {POST}: $(grep -c '^{HOOK_MARK}' {POST})"
        grep -e 'exec ' -e 'launch_panel -daemon' {BOOT} 2>/dev/null || echo 'no boot script'
        echo "launcher panels running: $(rs pidin -f n | grep -c launch_panel)"   # root's, so pidin as root
        rs tail -3 {DEST}/var/launch.log 2>/dev/null
    """, check=False)


# Puts the candidate $C (in STAGE) in place of POST, only when it is whole: it still has the image's last two steps,
# it has the lines it should ($N), and it parses. Then .new and a rename, so POST is the old file or the new, never half.
PUT_POST = f"""
    grep -q '^echo Process count' $C && grep -q '^exit 0' $C || {{ echo "{POST}: candidate incomplete, not installed"; exit 1; }}
    test $(( $(wc -l < $C) )) -eq "$N" || {{ echo "{POST}: candidate has $(( $(wc -l < $C) )) lines, not $N: not installed"; exit 1; }}
    sh -n $C
    rs cp $C {POST}.new
    rs chown root:root {POST}.new; rs chmod 555 {POST}.new
    rs mv {POST}.new {POST}
    sync
"""
WITHOUT_HOOK = f"grep -v '{HOOK_MARK}' {POST} | grep -v '^\\[ -x {BOOT} \\]'"


def boot(args):
    if not args:
        print(boot_state())
        return
    if args[0] == "on":
        opts = serve_options(args[1:])
        log, panel_log = f"{DEST}/var/serve.log", f"{DEST}/var/launch.log"
        with tempfile.TemporaryDirectory() as tmp:   # boot.sh is written here and copied to the target
            script = Path(tmp) / "boot.sh"
            script.write_text(
                "#!/bin/sh\n"
                f"# Starts the OpticNerve agent at boot (as root), and the launcher panel when it is installed, from the\n"
                f"# line `opticnerve_deploy.py boot on` added to\n"
                f"# {POST}. Written by that step; `boot off` deletes it.\n"
                "# A target whose image's own Screen does not suit keeps a script that restarts it in etc/screen.sh\n"
                "# (the QEMU guest without a GPU: host/qemu/guest_boot.sh); it runs first.\n"
                f"if [ -x {SCREEN_SH} ]; then echo \"boot: $(date): screen.sh\" >> {log}; {SCREEN_SH} >> {log} 2>&1; fi\n"
                f"waitfor /dev/screen/ 30 || {{ echo \"boot: $(date): no /dev/screen, agent not started\" >> {log}; exit 1; }}\n"
                "# The launcher panel, unless one runs (boot.sh run by hand): in the background, so it never holds up the\n"
                "# agent, once the image's own launcher is up (up to 30 s), so the panel opens over it. Started as\n"
                "# host/tools/launch_panel.sh start does; -daemon returns at once, and how it went is in launch.log.\n"
                f"if [ -x {DEST}/bin/launch_panel ] && ! pidin -f n | grep -q launch_panel; then\n"
                "\t(i=0; while [ $i -lt 30 ] && ! pidin -f n | grep -q demolauncher; do sleep 1; i=$((i + 1)); done\n"
                f"\techo \"boot: $(date): launch_panel\" >> {panel_log}\n"
                f"\tcd / && LD_LIBRARY_PATH=/proc/boot:/system/lib:/system/lib/dll {DEST}/bin/launch_panel -daemon "
                f"< /dev/null >> {panel_log} 2>&1) &\n"
                "fi\n"
                "test -e /dev/opticnerve && exit 0   # an agent runs already\n"
                f"echo \"boot: $(date): serve{opts}\" >> {log}\n"
                f"cd / && exec {DEST}/bin/opticnerve serve -passfile={DEST}/etc/vncpasswd{opts} < /dev/null > /dev/null "
                f"2>> {log}\n",
                newline="\n")
            remote(f"mkdir -p {STAGE} && chmod 700 {STAGE}")
            copy(script, f"{STAGE}/boot.sh")
        out = remote(f"""
            set -e
            test -x {DEST}/bin/opticnerve || {{ echo 'not installed'; exit 1; }}
            sh -n {STAGE}/boot.sh
            rs cp {STAGE}/boot.sh {BOOT}.new
            rs chown root:root {BOOT}.new; rs chmod 755 {BOOT}.new
            rs mv {BOOT}.new {BOOT}
            # the backup is the file without our lines: taken afresh whenever it has none (a later edit by anyone kept)
            test "$(grep -c '^{HOOK_MARK}' {POST})" -ne 0 || rs cp -p {POST} {POST_BACKUP}
            # the hook goes before the image's last step (its process count), once
            {WITHOUT_HOOK} > {STAGE}/without
            awk -v hook='{HOOK}' '/^echo Process count/ && !done {{ printf "%s", hook; done = 1 }} {{ print }}
                END {{ if (!done) exit 1 }}' {STAGE}/without > {STAGE}/post || {{ echo 'no "echo Process count" in {POST}: no hook added'; exit 1; }}
            C={STAGE}/post; N=$(( $(wc -l < {STAGE}/without) + 2 ))
            {PUT_POST}
            rm -rf {STAGE}
            diff {POST_BACKUP} {POST} || true
        """)
        print(out)
        print(f"boot on: {POST} starts {BOOT} (serve -passfile={DEST}/etc/vncpasswd{opts}, and the launcher panel "
              f"when installed); backup {POST_BACKUP}")
    elif args == ["off"]:
        out = remote(f"""
            set -e
            rs rm -f {BOOT}
            if [ "$(grep -c '^{HOOK_MARK}' {POST})" -eq 0 ]; then echo '{POST}: no hook, not changed'; exit 0; fi
            mkdir -p {STAGE} && chmod 700 {STAGE}
            {WITHOUT_HOOK} > {STAGE}/post
            C={STAGE}/post; N=$(( $(wc -l < {POST}) - 2 ))
            {PUT_POST}
            rm -rf {STAGE}
            if rs test -f {POST_BACKUP}; then
                if diff {POST_BACKUP} {POST} > /dev/null; then rs rm {POST_BACKUP}; echo '{POST}: as before boot on'
                else echo '{POST}: differs from {POST_BACKUP} (changed since boot on): backup kept'; fi
            fi
        """)
        print(out)
        print(f"boot off: {POST} no longer starts the agent; a running agent keeps running")
    else:
        sys.exit("usage: opticnerve_deploy.py boot [on [OPTION...] | off]")


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
    if remote(f"grep -c '^{HOOK_MARK}' {POST}", check=False).strip() not in ("", "0"):
        boot(["off"])   # the image's startup script as it was
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
    if len(sys.argv) < 2 or sys.argv[1] not in ("install", "start", "boot", "status", "pointer", "mouse", "stop", "undo"):
        sys.exit(__doc__)
    if not os.environ.get("ON_LOGIN_PASSWORD"):
        sys.exit("set ON_LOGIN_PASSWORD")
    cmd = sys.argv[1]
    if cmd == "install":
        install(sys.argv[2] if len(sys.argv) > 2 else ROOT / "agent" / "build" / "aarch64le" / "opticnerve")
    elif cmd == "start":
        start(sys.argv[2:])
    elif cmd == "boot":
        boot(sys.argv[2:])
    elif cmd in ("pointer", "mouse"):
        switch(cmd, sys.argv[2:])
    else:
        {"status": status, "stop": stop, "undo": undo}[cmd]()


if __name__ == "__main__":
    main()
