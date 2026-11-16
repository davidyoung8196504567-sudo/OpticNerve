# OpticNerve

![OpticNerve: eyes and hands on any QNX 8.0 screen](docs/brand/opticnerve-banner-serious.png)

**Gives your test tools eyes and hands on any QNX 8.0 screen.**

> Status: work in progress for the QNX Global Hackathon (November to December 2026). The agent builds for QNX 8.0
> (aarch64le and x86_64) and runs on a Raspberry Pi 5; OculiX sees and drives the Pi's screen through it.

## The problem

The HMI on a car dashboard, an infusion pump or a factory panel is tested by hand far more often than it should be.
GUI test tools run on a desktop host. To test an embedded target they need a way to see its screen and to act on it,
and teams either tap through test plans manually or build one-off rigs per product.

## The idea

OculiX (the open-source successor of SikuliX) automates GUIs by **seeing** the screen and **acting** on it, with
image matching and OCR. It runs on Windows, macOS and Linux. OpticNerve is the link to the target: the **OpticNerve
agent**, a small native program on the QNX 8.0 target that serves the screen to OculiX over VNC and tells a test
what it cannot learn from pixels alone.

```
  host (Windows / macOS / Linux)                     target (QNX 8.0, Raspberry Pi 5)
 +------------------------------+                   +---------------------------------------+
 |  OculiX 4.x test scripts     |   VNC (RFB 3.8)   |  opticnerve serve (our agent)         |
 |  image match, OCR, asserts   | <---------------> |   - reads the display (Screen API)    |
 +------------------------------+   port 5901       |   - injects pointer and keys (Screen) |
 |  helpers over SSH            | ----------------> |   - /dev/opticnerve: status, wait for |
 |   opticnerve status / wait   |                   |     a change, capture to a file       |
 +------------------------------+                   |  the app under test (any toolkit)     |
                                                    +---------------------------------------+
```

- **No Java on the target.** The agent is one native C program, `opticnerve`; the test logic stays on the host.
- **Toolkit-agnostic.** It works at the QNX Screen level, so it tests an app built with any GUI toolkit.
- **QNX-native.** One thread on a QNX dispatch loop: sockets, Screen's change signal, a timer and signals arrive as
  pulses and select events; `/dev/opticnerve` is a resource manager on the same loop.
- **Exact pixels.** Frames are 32 bits per pixel, read straight from the display: what the test compares is what
  the display shows.
- **Frames only on change.** The agent tracks, for each client and each 64x64 tile, what the client has not been
  sent yet, so a test can rely on a frame being current, and can ask (`opticnerve current`).

## Layout

| Folder | Contents |
|---|---|
| `agent/core/` | The decision-making part of the agent, plain C11 with no QNX header: RFB 3.8 with VNC authentication (our own DES), the client table, tile marks and the schedule of captures and updates |
| `agent/qnx/` | The QNX driver: dispatch loop, sockets, Screen capture and injection, `/dev/opticnerve`, and the subcommands |
| `agent/tests/` | Tests of the core (`core_test`) |
| `agent/spikes/` | Small test programs used to settle design questions on the target (input injection, window placement) |
| `host/tools/` | Deploy script, a VNC snapshot tool and a VNC input tool (Python) |
| `host/tests/`, `host/spikes/` | Network robustness tests; OculiX probes for reading and for input |
| `sdp/` | The pinned QNX SDP baseline and the script that checks a build host against it |
| `docs/` | [Configuration management](docs/CONFIG_MANAGEMENT.md) (everything needed to reproduce the setup) and the [OculiX issue list](docs/ISSUES.md) |

## What you need

| For | What | Used and tested here |
|---|---|---|
| Building | QNX SDP 8.0.5 from the QNX Software Center (the free QNX Everywhere licence), with `qcc`; the exact package list is `sdp/packages.lock` | Windows 11 host; baseline SDP-BL-006 |
| The target | Raspberry Pi 5 with the QNX 8.0 quick-start image (package `quickstart.rpi5` 0.5.0.00050, image 20260902), on the network, with its `qnxuser` login | Pi 5, image as shipped |
| Host tools | Python 3 with `cryptography` and `Pillow`; `ssh`, `scp` and `bash` | Python 3.14 on Windows (Git Bash) |
| OculiX | OculiX 4.0.0 (`oculixapi-4.0.0-<os>.jar`) and Java 17 or newer; for VNC, `tigervnc-java-oculix` 2.0.4 first on the classpath (configuration ON-VNC-1, below) | Temurin 25.0.4+7 |

## Build

1. Install the SDP and set its environment (`qnxsdp-env.bat` on Windows, `source qnxsdp-env.sh` on Linux), so that
   `QNX_HOST` and `QNX_TARGET` are set and `qcc` and `make` are on `PATH`.
2. Check the installation against the pinned baseline (optional, but it tells you the build host matches ours):

   ```
   python sdp/check_sdp.py <path to the SDP>
   ```

3. Build:

   ```
   cd agent
   make                  # both targets; or: make ARCH=aarch64le
   ```

   The results are `agent/build/<arch>/opticnerve` (the agent and its subcommands) and `agent/build/<arch>/core_test`.
   Everything builds with `-std=c11 -Wall -Wextra -Werror`. The aarch64le build runs on the Pi 5; the x86_64 build
   builds, and has not been run on an x86_64 target.

4. Run the core tests on the target (they need no display): copy `agent/build/aarch64le/core_test` to the Pi and run
   it. It prints one line per failed check and a total (`45 checks, 0 failed`). A build of the core tests for a
   desktop host is planned; it is not there yet.

## Run

Set three environment variables on the host first: `ON_HOST` (the Pi's address), `ON_LOGIN_PASSWORD` (the Pi's
`qnxuser` password, also used for `sudo`) and `ON_VNC_PASSWORD` (the password OpticNerve will ask VNC clients for, at
most 8 characters). Passwords travel to the Pi on the SSH connection's input, never on a command line.

```
python host/tools/opticnerve_deploy.py install     # copies the agent to /data/opticnerve/ and sets its password
python host/tools/opticnerve_deploy.py start       # starts the agent as root: VNC on port 5901
python host/tools/opticnerve_deploy.py status
```

Everything the agent needs lives in one folder on the Pi, `/data/opticnerve/`; nothing else on the target is changed.
To take it all off again:

```
python host/tools/opticnerve_deploy.py undo        # stops the agent and deletes /data/opticnerve/
```

**See the screen** with any VNC viewer at `<Pi address>:5901` and the VNC password, or with our tool:

```
python host/tools/vnc_snapshot.py <Pi address>:5901 "$ON_VNC_PASSWORD" frame.png
```

**Drive it from OculiX** (configuration ON-VNC-1):

- classpath: `tigervnc-java-oculix-2.0.4.jar` first, then `oculixapi-4.0.0-<os>.jar`;
- before the first connection, limit the security types: `SecurityClient.secTypes.setParam("VncAuth,None")`;
- connect with `VNCScreen.start(host, 5901, password, 10, 1000)`;
- send Enter as `type("\r")`, and use the `Mouse.*` constants for buttons.

`host/spikes/probe/ReadOnlyProbe.java` (reading) and `host/spikes/oculix_input/OculixInputProbe.java` (input) are
complete examples:

```
java -cp "tigervnc-java-oculix-2.0.4.jar;oculixapi-4.0.0-windows.jar" host/spikes/probe/ReadOnlyProbe.java <Pi address> "$ON_VNC_PASSWORD" out 5901
```

(`:` instead of `;` between the jars on Linux and macOS.)

**On the target**, any user can ask the agent:

| Command | What it does |
|---|---|
| `opticnerve status` | the status text: version, process ID, display, change count, captures, clients |
| `opticnerve wait [-timeout=S] [-since=COUNT] [-quiet=MS]` | waits for the next screen change (exit 0) or the timeout (exit 2); `-since` returns at once if the change count is already past COUNT; `-quiet` waits until the screen has settled (no change for MS); with `-since`, it first waits for a change past COUNT |
| `opticnerve current` | captures and says, for each VNC client, whether its last frame is current |
| `opticnerve capture X Y W H FILE` | writes a region of the screen to FILE (PPM), as the calling user |
| `opticnerve stop` | stops the agent (root only) |
| `opticnerve passwd [-passfile=PATH] < password` | sets the VNC password and tells the agent (root only) |

Every request has a time limit: an agent that does not answer makes a command print "agent not answering" and exit 3.

## Tests

| Test | Where | What it checks |
|---|---|---|
| `agent/tests/core_test.c` | on the target | DES and VNC authentication against independent values; the handshake and its refusals; the tile marks and the schedule (a change made while a client is busy, partial requests, the hold-off between captures, the timed capture); release of held keys and buttons on disconnect; message limits |
| `host/tests/robustness.py <Pi>:5901` | from a host | wrong password, unknown messages, a fifth client, silent connections, a client that stops reading, 208 connections closed at every protocol state |
| `host/spikes/oculix_input/OculixInputProbe.java` | from a host | 15 OculiX input calls through the agent; what arrives is logged by `agent/spikes/input_log.c` on the target |

## The demo

`agent/demo/demo_panel.c` is a small instrument panel for the target (three buttons, a counter, a lamp, a text line),
and `host/demo/DemoTest.java` is the OculiX test that drives it through the agent:

```
java -cp "tigervnc-java-oculix-2.0.4.jar;oculixapi-4.0.0-windows.jar" host/demo/DemoTest.java <Pi address> "$ON_VNC_PASSWORD" demo-out [-slow] [-fail] [-brief] [-repeat=N] [-cols=N]
```

The demo video was recorded with `host/demo/video/record_take.py`, which runs the shots of `host/demo/video/take.sh`
in a full-screen terminal while OBS records (Windows with Git for Windows and OBS Studio; the OBS scene and the webcam
settings are in [Configuration management](docs/CONFIG_MANAGEMENT.md)). `--dry` runs the shots without OBS.

## Settings and limits

- **Port 5901**, VNC authentication only (type 2), RFB 3.8 only, Raw encoding, 32 bits per pixel; a client that asks
  for another 32-bit pixel format gets it converted, and a client that asks for 8 or 16 bits per pixel is closed.
- **At most 4 clients** at once, plus 2 connections that are still logging in; a client that has not logged in after
  10 s is closed, and so is a client that reads nothing for 30 s while a frame is waiting for it.
- **Input:** the first client that sends input owns it until it disconnects; its held keys and buttons are released
  when it goes. Input from other clients is dropped (dropped keys are counted in the status).
- **Captures:** at most one at a time, and never sooner after the last one than that one took (at least 33 ms by
  default, `-holdoff=MS`); a timed capture every 2 s while a client waits for a change.
- **Who can do what on the target:** the agent runs as root. Any user who can log in to the target can read its
  status and capture the screen through `/dev/opticnerve` without the VNC password; input needs a VNC connection and
  the password. Stopping the agent and setting its password are root only. This fits a single-user test target.
- **Input reaches application windows only:** the agent injects input through Screen, which delivers it to the
  focused application (the demo panel, a Terminal). The quick-start image's launcher and window-manager shortcuts
  (Alt+Tab) take input from the input devices themselves, so a test starts applications over SSH instead
  (`host/demo/LoginTest.java` starts the launcher's Terminal as the launcher does). Injected keys carry the same key
  symbol and USB scan code as a real keyboard's.
- **Not addressed:** QNX security policies; encryption (VNC authentication is not meant for untrusted networks).
- **Not included:** boot images (the agent is deployed onto the quick-start image as it is shipped); start at boot.

## Future work

### A redaction layer

Hide sensitive areas of the screen from everything that leaves the target (tests, recordings, remote viewers),
updated live as the screen changes: a login screen, a generated key, a line on a censor list.

- **Where:** in the agent's frame stream. The agent already reads every frame and tracks changes in 64x64 tiles for
  each client; before a frame is sent, listed rectangles are blacked out or blurred. Nothing hidden leaves the
  target, and the rest of the frame stays exact. (An overlay window on the target's own display does not fit the
  quick-start image, where application windows are shown full screen.)
- **Detection has to happen on the target.** OCR in the test tool on the host would need the pixels before deciding
  to hide them. In order of effort:
  1. rules by application or window, which Screen reports to the agent ("when the Terminal shows `Password:`, hide
     the line after it"), and fixed rectangles;
  2. template matching in C on the frame (a known field, a known logo);
  3. OCR on the target with a censor list. This needs an OCR engine on QNX and adds latency to every changed
     region.
- **No leak while deciding:** a changed region's tiles are held back until detection has run on them, rather than
  sent and masked afterwards. The per-client tile marks make this a natural extension.
- **A first step:** `opticnerve redact X Y W H` (and `unredact`), a rectangle the agent blacks out in every frame it
  sends, set by a script on the target. Estimate for rules and rectangles: a few days, most of it tests.

Typed passwords are not the main case: a login prompt does not echo them. The cases are text that is shown, such as
a default password in a message of the day.

### OCR with word positions: Octachorix

[Octachorix](https://github.com/oculix-org/Octachorix), from the same family of repositories as OculiX, is a
Tesseract runtime for the JVM that returns text, word positions and confidences in one pass, and loads exactly the
native library it is given (paired with [Legerix](https://github.com/oculix-org/Legerix), which ships those
libraries). Word positions are what both the redaction layer's OCR rules and our OCR checks need (today
`LoginTest.java` compares whole-screen text). Octachorix is GPL-3.0 and OpticNerve is MIT, which matters if it is
ever bundled; using it from a test is the simpler path.

### Input below Screen

The agent's input reaches application windows only (see Settings and limits). Driving the launcher and
window-manager shortcuts would need input from a virtual input device at the driver level, which looks to the system
like a real keyboard and mouse. Not tried yet.

## Licence

MIT. See [LICENSE](LICENSE).
