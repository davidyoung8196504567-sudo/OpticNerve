# OpticNerve design (phase 1)

Status: **accepted by David on 2026-11-12**, including the native Screen demo HMI (see "Scope for the demo") and
the phase changes at the end, which are now in [PLAN.md](PLAN.md).

Evidence: [notes/2026-11-12_pi5_first_boot.md](notes/2026-11-12_pi5_first_boot.md) (what `vncserv` does on the
Pi 5), the Screen API reference installed with the SDP (`target/qnx/usr/include/screen/screen.h` and the Screen
docs under `target/qnx/usr/help/eclipse/plugins/com.qnx.doc.screen/`), and Oculix's own documentation.

## Decision

**Pixels and input go through QNX's own `vncserv`; the ON agent does what `vncserv` cannot.** We do not write or
port a VNC server.

Why:

- `vncserv` (package `target.screen.vncserv`, pinned in our SDP baseline) already runs by default on the Pi 5
  quick-start image. On 2026-11-12 it served the whole 1920 x 1080 display, including OpenGL ES, Vulkan, a video
  layer and the Weston/XFCE desktop, and it forwarded keys, pointer moves and clicks (menus, submenus and browser
  buttons all responded).
- Oculix documents a VNC client: `VNCScreen.start(host, port, password, ...)`, built on a TigerVNC Java viewer, with
  VNC password authentication, which is what `vncserv` uses. So Oculix can talk to `vncserv` directly. Not yet
  tested against `vncserv` (spike S1).
- Writing or porting an RFB server would spend most of the time to 2026-12-15 on something that already works.

What `vncserv` does not do, seen on day 1, is where the agent earns its place:

- It cannot tell the tester whether a frame is current. We captured one completely black frame while the monitor
  showed the desktop.
- It does nothing about screen blanking and the lock screen, which make a test fail for the wrong reason.
- It knows nothing about windows, so a test can only find things by image matching.
- It offers no status or control a test can query.

## Architecture

```
 host (Windows / Linux)                                target (QNX 8.0: Pi 5 or QEMU)
 +-------------------------+   VNC (RFB 3.8), :5900    +---------------------------------+
 | Oculix test script      | <-----------------------> | vncserv (QNX)                   |
 |  VNCScreen: see, click  |   frames out, input in    |   screen capture, input to Screen|
 |                         |                           +---------------------------------+
 |  on-status, on-capture  |   SSH, :22                | on (CLI)  --message passing-->  |
 |  (helpers over SSH)     | ------------------------> | ON agent: resource manager      |
 +-------------------------+                           |   /dev/opticnerve               |
                                                       |   Screen API (privileged ctx)   |
                                                       +---------------------------------+
```

## The ON agent

A native C program on the target, running as root, registered as a resource manager at `/dev/opticnerve`. Every
feature below names the Screen API it relies on (from `screen.h`) and the day-1 problem it answers.

| Feature | Screen API | Problem it answers |
|---|---|---|
| **Frame freshness**: count compositions; report "display updated N times since T" and the time of the last update | `screen_notify(ctx, SCREEN_NOTIFY_UPDATE, display, &sigevent)` ("notification when Screen completes composition"); `SCREEN_DISPLAY_METRIC_UPDATE_COUNT` via `SCREEN_PROPERTY_METRICS` | the black frame; lets a test wait until the screen has settled before matching |
| **Idle and power state**: report whether the display is idle and its power mode | `SCREEN_PROPERTY_IDLE_STATE` (read-only), `SCREEN_PROPERTY_POWER_MODE`, `SCREEN_EVENT_IDLE` | blanking during a test is detected and reported, not mistaken for a failure |
| **Keep-awake** during a test session | a visible window with `SCREEN_PROPERTY_IDLE_MODE = SCREEN_IDLE_MODE_KEEP_AWAKE` | Screen's own idle blanking. Weston's lock screen is a separate layer (see "Scope") |
| **Region capture** for assertions, independent of VNC: write a region of the display to a file | `screen_read_display()` with a `SCREEN_DISPLAY_MANAGER_CONTEXT` (needs effective root and write access to `/dev/screen/.dpymgr`) | a second, direct view of the screen to cross-check `vncserv`'s frames |
| **Status and control** | resource manager (`resmgr_attach`, message passing) | the test can ask "is the target ready?" |
| *Window list (stretch)*: id, owner pid, position, size, visibility | `SCREEN_WINDOW_MANAGER_CONTEXT`, `SCREEN_EVENT_CREATE`, `SCREEN_PROPERTY_ID_STRING`, `_OWNER_PID`, `_POSITION`, `_SIZE`, `_VISIBLE` | find an app's window without image matching. Unproven: a window manager is already running (spike S3) |

Input injection is **not** in the agent: `vncserv` already forwards keys and pointer to Screen, and its
`-use_touch` option turns mouse buttons into touch contacts. `screen_inject_event()` stays available if a spike
shows a gap.

### Interface

- `/dev/opticnerve` answers reads with a status text (frame counter, last update time, idle state, power mode,
  keep-awake on or off, agent version). Control goes through `devctl()` messages: keep-awake on or off, capture a
  region to a file, reset counters.
- A small `on` command on the target wraps those messages (`on status`, `on keepawake on`, `on capture x y w h
  file.png`, `on wait-update --timeout 2s`).
- The host reaches it over SSH (`ssh qnxuser@pi on status`), so no new network service is opened. The Oculix suite
  calls these through small host helpers.

## Privileges

- The agent runs as **root**: `screen_read_display()` requires effective root, and the privileged contexts are
  gated by write access to `/dev/screen/.dpymgr`, `.winmgr` and the other nodes.
- The `on` command runs as `qnxuser` and only sends messages; the resource manager decides what to allow.
- QNX security policies are not addressed in the hackathon build. Said so in the README.

## Scope for the demo

Decided: the demo HMI is a **native Screen application** (C, Screen API), not an app under the Weston/XFCE
desktop. That keeps the keep-awake and capture paths simple and shows the QNX-specific stack. The desktop still
works over `vncserv` (shown on day 1), and Weston's own idle and lock settings (`idle-time`, `locking=true` in
`~/.config/weston.ini`) can be configured for test runs as a recorded target change.

## Spikes before building (each a yes/no, time-boxed)

Nothing in these may change a target without being recorded.

Results for S2 to S6 (2026-11-12): [notes/2026-11-12_spikes.md](notes/2026-11-12_spikes.md). In short: capture and
update notifications work; the window list and keep-awake do not apply on this image; `vncserv` resends full frames
even when nothing changed. The changes they suggest to this design are proposed there, not yet made here.

| # | Question | How |
|---|---|---|
| S1 | Does Oculix's `VNCScreen` connect to `vncserv`, see the screen and click? | Oculix on Windows against the Pi: open the Applications menu, as the Python tools did |
| S2 | Does `screen_read_display()` as root capture the same pixels as `vncserv`, and does it include the Weston surface and the cursor? | a 50-line C test built with qcc, run once on the Pi |
| S3 | Can a `SCREEN_WINDOW_MANAGER_CONTEXT` be created and receive `SCREEN_EVENT_CREATE` while `fullscreen-winmgr` or Weston is the window manager? (`screen_register_window_manager()` fails if a manager exists) | same test program |
| S4 | Does a keep-awake window stop blanking in the native session? What stops it under Weston? | test program plus a timed wait |
| S5 | Does `screen_notify(SCREEN_NOTIFY_UPDATE)` fire per composition, and did the black frame coincide with an idle or power state? | test program logging updates and idle state |
| S6 | `vncserv` frame rate with a viewer that stays connected (incremental updates), and its CPU load | Oculix or the Python tools in a loop |

## Risks and tunables

- **`vncserv` is closed QNX code** we do not control. If it misbehaves we can only reconfigure it or replace it.
  Replacing it is the fallback (own RFB server on `screen_read_display()` and `screen_inject_event()`), not the
  plan.
- **Pixel format:** `vncserv` reads back as `rgb565` by default. `-format=rgba8888` is available; changing it means
  editing `/system/etc/startup/post_startup.sh` on the target, a recorded target change. Decide after S1 and S6.
- **Frame rate:** a one-shot full frame took about 0.3 s; with a stay-connected viewer it should be faster
  (unmeasured, S6).
- **Oculix version:** the README says Oculix 4.x; the sources found on 2026-11-12 describe 3.0.x builds. Pin the
  exact release and its hash in the config management doc before S1.
- **Deployment:** the agent goes onto the target in a reproducible, recorded way (a script that copies the
  binary and records its hash), so the build-and-run instructions the rules require stay true.

## Changes to the plan (accepted 2026-11-12)

- Phase 2 "Capture" and phase 3 "Input" as first written assumed we build the VNC server. Instead:
  - Phase 2: spikes S1 to S6, then the agent's frame-freshness, idle and keep-awake features.
  - Phase 3: region capture, the `on` command and the host helpers over SSH.
- Phase 4: the resource manager is now part of phases 2 and 3, since it is the agent's core. Phase 4 becomes the
  window list (if S3 allows) and hardening.
- Phases 5 and 6 unchanged.
