# OpticNerve design (phase 1)

The server and agent design is now [DESIGN_SERVER.md](DESIGN_SERVER.md), which is in review R-001 (gate G1). This
page is the earlier design of 2026-11-12, kept for the record.

Status: **accepted by David on 2026-11-12**, including the native Screen demo HMI (see "Scope for the demo") and
the phase changes at the end, which are now in [PLAN.md](PLAN.md).

Evidence: [notes/2026-11-12_pi5_first_boot.md](notes/2026-11-12_pi5_first_boot.md) (the image, `vncserv` and its
options on the Pi 5), the Screen API reference installed with the SDP (`target/qnx/usr/include/screen/screen.h` and
the Screen docs under `target/qnx/usr/help/eclipse/plugins/com.qnx.doc.screen/`), and Oculix's own documentation.

## Decision

**Pixels and input go through QNX's own `vncserv`; the ON agent adds frame freshness, display state, region capture
and status.** We do not write or port a VNC server.

Why:

- `vncserv` (package `target.screen.vncserv`, pinned in our SDP baseline) already runs by default on the Pi 5
  quick-start image, started as `vncserv -display=1 -auth` on port 5900, for the 1920 x 1080 display.
- Oculix documents a VNC client: `VNCScreen.start(host, port, password, ...)`, built on a TigerVNC Java viewer, with
  VNC password authentication, which is what `vncserv` uses. So Oculix can talk to `vncserv` directly. Not yet
  tested against `vncserv` (spike S1).
- Writing or porting an RFB server would spend most of the time to 2026-12-15 on something the image already
  provides.

What the agent adds:

- a frame-freshness signal, so a test knows when the screen has been updated;
- display idle and power state, and handling of blanking and the lock screen;
- (stretch) a window list, so a test can find an app's window without image matching;
- status and control that a test can query.

## Architecture

```
 host (Windows / Linux)                                target (QNX 8.0: Pi 5 or QEMU)
 +-------------------------+   VNC (RFB 3.8), :5900    +---------------------------------+
 | Oculix test script      | <-----------------------> | vncserv (QNX)                   |
 |  VNCScreen: see, click  |   frames out, input in    |   screen capture, input to Screen|
 |                         |                           +---------------------------------+
 |  opticnerve status, ... |   SSH, :22                | opticnerve (CLI) --messages-->  |
 |  (helpers over SSH)     | ------------------------> | ON agent: resource manager      |
 +-------------------------+                           |   /dev/opticnerve               |
                                                       |   Screen API (privileged ctx)   |
                                                       +---------------------------------+
```

## The ON agent

A native C program on the target, running as root, registered as a resource manager at `/dev/opticnerve`. Every
feature below names the Screen API it relies on (from `screen.h`) and what it is for.

| Feature | Screen API | Purpose |
|---|---|---|
| **Frame freshness**: count compositions; report "display updated N times since T" and the time of the last update | `screen_notify(ctx, SCREEN_NOTIFY_UPDATE, display, &sigevent)` ("notification when Screen completes composition"); `SCREEN_DISPLAY_METRIC_UPDATE_COUNT` via `SCREEN_PROPERTY_METRICS` | lets a test wait until the screen has settled before matching |
| **Idle and power state**: report whether the display is idle and its power mode | `SCREEN_PROPERTY_IDLE_STATE` (read-only), `SCREEN_PROPERTY_POWER_MODE`, `SCREEN_EVENT_IDLE` | blanking during a test is detected and reported, not mistaken for a failure |
| **Keep-awake** during a test session | a visible window with `SCREEN_PROPERTY_IDLE_MODE = SCREEN_IDLE_MODE_KEEP_AWAKE` | Screen's own idle blanking. Weston's lock screen is a separate layer (see "Scope") |
| **Region capture** for assertions, independent of VNC: write a region of the display to a file | `screen_read_display()` with a `SCREEN_DISPLAY_MANAGER_CONTEXT` (needs effective root and write access to `/dev/screen/.dpymgr`) | a second, direct view of the screen for assertions |
| **Status and control** | resource manager (`resmgr_attach`, message passing) | the test can ask "is the target ready?" |
| **Component versions on screen** (David, 2026-11-15): a small label in the launcher's bottom bar, next to the IP address and image ID, showing the agent's version and a text set for the test run (David, later the same day: the label shows our own software only); the same in the status text | a small visible window: `screen_create_window()`, `SCREEN_PROPERTY_POSITION`, `_SIZE`, `_ZORDER` above the launcher, text drawn into its buffer with a built-in bitmap font | the agent's version and the run's name are visible in every frame, the camera recording and the demo video. The label's rectangle is fixed, so tests exclude it from pixel comparisons |
| *Window list (stretch)*: id, owner pid, position, size, visibility | `SCREEN_WINDOW_MANAGER_CONTEXT`, `SCREEN_EVENT_CREATE`, `SCREEN_PROPERTY_ID_STRING`, `_OWNER_PID`, `_POSITION`, `_SIZE`, `_VISIBLE` | find an app's window without image matching. Unproven: a window manager is already running (spike S3) |

Input injection is **not** in the agent: keys and pointer go in through `vncserv` (its usage text: "Events from the
keyboard and mouse are forwarded to Screen"; its `-use_touch` option turns mouse buttons into touch contacts).
`screen_inject_event()` stays available to the agent.

### Interface

- `/dev/opticnerve` answers reads with a status text (frame counter, last update time, idle state, power mode,
  keep-awake on or off, agent version). Control goes through `devctl()` messages: keep-awake on or off, capture a
  region to a file, reset counters.
- A small `opticnerve` command on the target wraps those messages (`opticnerve status`, `opticnerve keepawake on`,
  `opticnerve capture x y w h file.png`, `opticnerve wait -timeout=2`).
- The host reaches it over SSH (`ssh qnxuser@pi opticnerve status`), so no new network service is opened. The Oculix
  suite calls these through small host helpers.

## Privileges

- The agent runs as **root**: `screen_read_display()` requires effective root, and the privileged contexts are
  gated by write access to `/dev/screen/.dpymgr`, `.winmgr` and the other nodes.
- The `opticnerve` command runs as `qnxuser` and only sends messages; the resource manager decides what to allow.
- QNX security policies are not addressed in the hackathon build. Said so in the README.

## Scope for the demo

Decided: the demo HMI is a **native Screen application** (C, Screen API), not an app under the Weston/XFCE
desktop. That keeps the keep-awake and capture paths simple and shows the QNX-specific stack. Weston's own
idle and lock settings (`idle-time`, `locking=true` in `~/.config/weston.ini`) can be configured for test runs as a
recorded target change.

## Spikes before building (each a yes/no, time-boxed)

Nothing in these may change a target without being recorded.

S2 to S6 were run on the Pi on 2026-11-12 with `agent/spikes/screen_probe.c` and the host tools (build command and
target change: [CONFIG_MANAGEMENT.md](CONFIG_MANAGEMENT.md), row "Spike test program on the Pi target"). Design
changes that followed, proposed and not yet made here: freshness through `screen_notify()` only; no keep-awake
window and no window list in the agent.

| # | Question | How |
|---|---|---|
| S1 | Does Oculix's `VNCScreen` connect to `vncserv`, see the screen and click? | Oculix on Windows against the Pi: `host/spikes/s1/S1VncScreen.java` |
| S2 | Can the agent capture the display with `screen_read_display()` as root, a region as well as the whole, with the Weston surface and the cursor? | a C test program built with qcc (`agent/spikes/screen_probe.c`), run on the Pi |
| S3 | Can a `SCREEN_WINDOW_MANAGER_CONTEXT` be created and receive `SCREEN_EVENT_CREATE` while `fullscreen-winmgr` or Weston is the window manager? | same test program |
| S4 | Does the agent need a keep-awake window in the native session, and under Weston? | test program plus a timed wait |
| S5 | Does `screen_notify(SCREEN_NOTIFY_UPDATE)` deliver a pulse when the display is updated? | test program logging updates and idle state |
| S6 | Can the host tools stay connected and ask for incremental updates, like a viewer that does not reconnect for every frame? | `host/tools/vnc_rate.py` |

## Risks and tunables

- **`vncserv` is a QNX binary**: we configure it through its options and do not change it. The fallback is our own
  RFB server (on `screen_read_display()` and `screen_inject_event()`), not the plan.
- **Pixel format:** `vncserv` reads back as `rgb565` by default. `-format=rgba8888` is available; changing it means
  editing `/system/etc/startup/post_startup.sh` on the target, a recorded target change. Decide after S1 and S6.
- **Oculix version:** the README says Oculix 4.x; the sources found on 2026-11-12 describe 3.0.x builds. Pin the
  exact release and its hash in the config management doc before S1.
- **Deployment:** the agent goes onto the target in a reproducible, recorded way (a script that copies the
  binary and records its hash), so the build-and-run instructions the rules require stay true.

## Changes to the plan (accepted 2026-11-12)

- Phase 2 "Capture" and phase 3 "Input" as first written assumed we build the VNC server. Instead:
  - Phase 2: spikes S1 to S6, then the agent's frame-freshness, idle and keep-awake features.
  - Phase 3: region capture, the `opticnerve` command and the host helpers over SSH.
- Phase 4: the resource manager is now part of phases 2 and 3, since it is the agent's core. Phase 4 becomes the
  window list (if S3 allows) and hardening.
- Phases 5 and 6 unchanged.
