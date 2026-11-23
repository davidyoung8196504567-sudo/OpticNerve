# Settings: addresses, ports and passwords

Every script finds the target's addresses, ports, login and passwords the same way. For each value it takes the
first of:

1. the script's command line, where it has a parameter for the value;
2. the environment (`export ON_HOST=...`);
3. the settings file, `~/.config/opticnerve/env` (or the file named by `ON_CONFIG`);
4. the default in the tables below.

Make the settings file from the template [`host/tools/opticnerve.env.example`](../host/tools/opticnerve.env.example),
readable only by you (`chmod 600`). `ON_CONFIG`, if set, is an absolute path. Never commit a filled-in copy.

The file's grammar, the same in every reader (`host/tools/on_env.sh` for the shell scripts, `host/tools/on_env.py`
for Python, `cfg()` in each Java test and in the OculiX IDE scripts): a line is `[export] NAME=VALUE [# comment]`,
with `NAME` starting with `ON_`, and `VALUE` one of `'text'`, `"text"` or text without spaces, `#` or quotes. Values
are taken literally: no `$` expansion, no commands. A line that doesn't fit (a value with a space but no quotes, a
`'` inside `'...'`) is skipped, so quote every value and keep `'` out of the passwords. An empty value counts as
unset. The first line with a value wins.

Where a script takes the target and the VNC password as positional parameters, a `-` in their place takes them from
the settings: `python host/tools/vnc_snapshot.py - - frame.png`. That keeps the password out of the process list.

## The settings

| Variable | What it is | Default |
|---|---|---|
| `ON_HOST` | The target's address for VNC (the OpticNerve agent) | `10.30.1.116` (the Pi 5) |
| `ON_VNC_PORT` | The agent's VNC port, as this host reaches it | `5901` |
| `ON_SSH_HOST` | The target's address for SSH, when it differs from `ON_HOST` (a port forward, a tunnel) | `ON_HOST` |
| `ON_SSH_PORT` | The target's SSH port | `22` |
| `ON_LOGIN_USER` | The SSH login on the target, also used for `sudo` | `qnxuser` |
| `ON_LOGIN_PASSWORD` | That login's password | none: needed for SSH |
| `ON_VNC_PASSWORD` | The agent's VNC password | none: needed for VNC |
| `ON_QEMU_HOST` | The Linux host running the QEMU guest (`host/qemu/tunnel.sh`) | `10.30.1.104` |
| `ON_GUEST` | The QEMU guest's address on that host | `192.168.122.146` |
| `ON_UBUNTU_PASSWORD` | The `tester` login on `ON_QEMU_HOST` | none |
| `ON_OBS_HOST`, `ON_OBS_PORT` | OBS Studio's WebSocket server | `127.0.0.1`, `4455` |
| `ON_OBS_PASSWORD` | Its password | read from OBS's own settings |
| `ON_JAVA`, `ON_OCULIX_JAR` | The PoC tester's JDK 17 `java` and OculiX IDE jar | `~/opticnerve-work/jdk/jdk17/bin/java`, `~/qtb1/oculixide-5.0.0-complete-lux.jar` |
| `ON_TESTS`, `ON_LOGS` | The PoC tester's test folder and log folder | `~/ide-tests`, `~/ide-session` |

`ON_SSH_HOST` follows `ON_HOST` unless it is set itself, so leave it out of the settings file unless SSH really goes
elsewhere. Example, QEMU with user-mode networking and forwarded ports (VNC on 5901, SSH on 2222):
`ON_HOST='127.0.0.1'`, `ON_SSH_PORT='2222'`.

The agent itself listens on port 5901 unless it is started with `serve -port=P`. `ON_VNC_PORT` is the port this host
connects to, which differs from 5901 only behind a port forward.

## The scripts

The **parameter** column is what the command line takes; **setting** is the variable a parameter falls back to, or
that the script reads with no parameter of its own.

| Script | Parameter | Setting it maps to | Default |
|---|---|---|---|
| `host/tools/pi_ssh.sh 'COMMANDS'` | (none) | `ON_SSH_HOST`, `ON_SSH_PORT`, `ON_LOGIN_USER`, `ON_LOGIN_PASSWORD` | see above |
| `host/tools/opticnerve_deploy.py install\|start\|status\|stop\|undo` | (none) | `ON_SSH_HOST`, `ON_SSH_PORT`, `ON_LOGIN_USER`, `ON_LOGIN_PASSWORD`; `ON_VNC_PASSWORD` for `install` | see above |
| `host/tools/demo_panel.sh start\|stop\|status` | (none) | as `pi_ssh.sh` | see above |
| `host/tools/vnc_snapshot.py HOST PASSWORD OUT.png` | `HOST[:PORT]` or `-` | `-`: `ON_HOST`, `ON_VNC_PORT` | port 5900 when `HOST` has none |
| | `PASSWORD` or `-` | `-`: `ON_VNC_PASSWORD` | |
| `host/tools/vnc_input.py HOST PASSWORD ACTION...` | `HOST[:PORT]` or `-`, `PASSWORD` or `-` | as `vnc_snapshot.py` | port 5900 |
| `host/tools/vnc_rate.py HOST PASSWORD SECONDS` | `HOST[:PORT]` or `-`, `PASSWORD` or `-` | as `vnc_snapshot.py` | port 5900 |
| `host/tools/vnc_latency.py HOST PASSWORD [ROUNDS]` | `HOST[:PORT]` or `-`, `PASSWORD` or `-` | as `vnc_snapshot.py` | port 5901 |
| `host/tools/obs_control.py ...` | (none) | `ON_OBS_HOST`, `ON_OBS_PORT`, `ON_OBS_PASSWORD` | `127.0.0.1`, `4455` |
| `host/tests/robustness.py [HOST[:PORT]]` | `HOST[:PORT]`, or none or `-` | `ON_HOST`, `ON_VNC_PORT`; `ON_VNC_PASSWORD` | port 5901 |
| `host/demo/DemoTest.java HOST PASSWORD ...` (and `DesktopTest`, `GtkDemoTest`, `LoginTest`, `ObstacleCourse`) | `HOST` or `-` | `-`: `ON_HOST` | |
| | `PASSWORD` or `-` | `-`: `ON_VNC_PASSWORD` | |
| | (none) | `ON_VNC_PORT`; SSH: `ON_SSH_HOST` (default `HOST`), `ON_SSH_PORT`, `ON_LOGIN_USER`, `ON_LOGIN_PASSWORD` | 5901; 22 |
| `host/demo/DemoTest.sikuli`, `host/ide-tests/Template.sikuli` (OculiX IDE) | (none) | `ON_HOST`, `ON_VNC_PORT`, `ON_VNC_PASSWORD` | 5901 |
| `host/demo/run_watched.sh NAME -- COMMAND` | (none) | `ON_HOST`, `ON_VNC_PORT`, `ON_VNC_PASSWORD`; SSH as `pi_ssh.sh` | |
| `host/demo/video/take.sh` | (none) | `ON_HOST`; SSH as `pi_ssh.sh` | |
| `host/qemu/tunnel.sh [stop]` | (none) | `ON_QEMU_HOST`, `ON_GUEST`, `ON_UBUNTU_PASSWORD` | `10.30.1.104`, `192.168.122.146` |
| `tests/visible/suite.py` | (none) | `ON_HOST` (the image's own `vncserv`, port 5900), `ON_VNC_PASSWORD`; SSH as `pi_ssh.sh` | |
| `host/poc/ide.sh` | (none) | `ON_JAVA`, `ON_OCULIX_JAR`, `ON_TESTS`, `ON_LOGS`; passes all settings to the IDE | |
| `host/poc/viewer.sh [HOST[::PORT]]` | `HOST[::PORT]` | `ON_HOST`, `ON_VNC_PORT`; `ON_VNC_PASSWORD` for that address only | |
| `host/poc/ssh-pi.sh [HOST[:PORT]\|- [COMMAND...]]` | `HOST[:PORT]` or `-` | `ON_SSH_HOST`, `ON_SSH_PORT`, `ON_LOGIN_USER`; `ON_LOGIN_PASSWORD` for that address only | port 22 when `HOST` has none |
| `host/poc/demo-panel.sh [start\|stop\|status]` | (none) | as `demo_panel.sh` | |
| `host/poc/run-test.sh [TEST]` | (none) | `ON_JAVA`, `ON_OCULIX_JAR`, `ON_TESTS`, `ON_LOGS`; the test reads its own | |
| `host/poc/sut.sh build\|install\|start\|stop\|status\|undo [HOST[:PORT]]` | `HOST[:PORT]` | `ON_SSH_HOST`, `ON_SSH_PORT`; then as `opticnerve_deploy.py` | port 22 when `HOST` has none |

Not changed: the experiments in `host/spikes/` (kept as they were run; they take the host and port on the command
line), and `host/qemu/qemu-run-tcg.sh` (it starts QEMU, no address).
