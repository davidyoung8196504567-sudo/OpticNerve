# PoC demo commands

The commands of the proof-of-concept demo, run on the tester (a Linux PC with the OculiX IDE) against the target (a
QNX Raspberry Pi 5 with the OpticNerve agent). Each one works from a terminal and from a desktop shortcut.

| | Command | What it does |
|---|---|---|
| 1 | `sh host/poc/ide.sh` | Starts the OculiX IDE. It opens as a tray icon: open a workspace from it. Tests started from it can read the target's address and passwords and start the demo panel |
| 2 | `sh host/poc/viewer.sh [HOST[::PORT]]` | Opens OculiX's embedded VNC viewer (TigerVNC Java) **with control**, on the agent (port 5901) or on any VNC server (`HOST::5900`). The agent's saved password is used for `ON_HOST::5901` only; for other servers set `VNC_PASSWORD` or type it. Without HOST it asks for the address and the password (Enter keeps the saved one) |
| 3 | `sh host/poc/ssh-pi.sh [HOST\|- [COMMAND...]]` | An SSH session on the Pi as `qnxuser` (root commands with `sudo`); `-` is the saved address, e.g. `ssh-pi.sh - pidin -f n` |
| 4 | `sh host/poc/demo-panel.sh [start [-marker] [SECS] \| stop \| status]` | Starts a fresh demo panel on the Pi (count 0, lamp GREEN, an hour, no pointer marker), replacing any running one |
| 5 | `sh host/poc/run-test.sh [TEST.sikuli]` | Runs a recorded OculiX test without the IDE and prints PASS or FAIL (exit 0 or 1). FAIL: the script stopped with an error, or a test made from `Template.sikuli` printed its "N FAILED" line. A bare name is looked for in `~/ide-tests`; without one it lists the tests there. **Close the IDE first**: it refuses while one runs, because two OculiX processes on one desktop crash each other within seconds (a SIGSEGV in OculiX's hotkey listener, JXGrabKey), and an unsaved test in the IDE is lost |
| | `sh host/poc/sut.sh build` | Checks the QNX SDP against the baseline (`sdp/check_sdp.py`), then builds the agent and the demo panel for the Pi |
| | `sh host/poc/sut.sh install [HOST]` | Replaces the agent and the demo panel on the target and starts the agent (no login limit); asks first. Also `start`, `stop`, `status`, and `undo` (removes both) |
| | `sh host/poc/install-shortcuts.sh [-remove]` | Puts a shortcut for each command on the desktop and in the applications list |

Paths are from this repository's folder; the scripts find the repository themselves, so they run from anywhere.
Logs go to `~/ide-session/`.

## Set up a tester

1. Clone this repository, and get a JDK 17, the OculiX IDE jar (we use `oculixide-5.0.0-complete-lux.jar`, the
   QTB-1 build) and Python 3 with `cryptography` (`apt install python3-cryptography`; the viewer's password file).
   For a build of the agent on the tester, install the QNX SDP at the baseline in `sdp/`.
2. Put the settings in `~/.config/opticnerve/env`, readable only by you (`chmod 600`). It is read as shell, so
   put each value in single quotes; the passwords cannot contain `'`:

   ```
   ON_HOST='<the Pi's address>'
   ON_VNC_PASSWORD='<the agent's VNC password>'
   ON_LOGIN_PASSWORD='<the Pi's qnxuser password>'
   ```

   If the JDK, the jar, the tests or the SDP are not where `poc-env.sh` expects them, set `ON_JAVA`,
   `ON_OCULIX_JAR`, `ON_TESTS` or `QNX_SDP_DIR` in `~/.config/opticnerve/poc.conf`.
3. `sh host/poc/install-shortcuts.sh`. If GNOME shows a shortcut as untrusted, right-click it and choose
   "Allow Launching".

To put the agent on a new Pi: `sut.sh build`, then `sut.sh install`. Record the SHA-256 that `install` prints in
[docs/CONFIG_MANAGEMENT.md](../../docs/CONFIG_MANAGEMENT.md).

## A test session

`ide.sh`, then `viewer.sh` to work the Pi's apps and capture images, and `demo-panel.sh` for a fresh panel (a test
can also start it itself: see [host/ide-tests/README.md](../ide-tests/README.md)). The viewer and the test take
turns at the Pi's input, so keep your hands off the viewer while a test runs. `run-test.sh` replays a saved test
with the IDE closed.
