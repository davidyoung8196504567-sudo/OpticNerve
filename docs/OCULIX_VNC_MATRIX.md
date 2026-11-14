# What OculiX 4.0.0 can do: local host, over VNC, over VNC to QNX 8

Written 2026-11-13. This is the measurement table for OpticNerve: it shows which OculiX functions reach a QNX 8
target through QNX's own VNC server (`vncserv`), and what the ON agent has to add. It is a first version: most
cells are not measured yet, and each cell says what it rests on.

## Sources

- **OculiX 4.0.0 source** at tag `v4.0.0` (<https://github.com/oculix-org/Oculix>): `org/sikuli/script/Region.java`,
  `org/sikuli/vnc/VNCScreen.java`, `VNCRobot.java`, the README and release notes; the VNC client classes
  (`com.sikulix.vnc.VNCClient`, `VNCFrameBuffer`, `VNCClipboard`) from the pinned jar, since their source is in a
  separate repository (`tigervnc-java-oculix`).
- **ON-UBUNTU24's read-only run** from UBUNTU24 against the Pi (2026-11-14): `oculixapi-4.0.0-linux.jar` (SHA-256
  `bfebf784...`, the published digest), Temurin 25.0.4+7; no input sent.
- **ON-UBUNTU24's run against a non-QNX server** (2026-11-14): TigerVNC `Xtigervnc` 1.13.1+dfsg-2build2 (Ubuntu
  noble packages, unpacked without root), private display :5, 1920 x 1080, depth 24 (32 bits per pixel), port 5905,
  localhost only, VncAuth; a blue background, an `xmessage` window with 3 lines of text, `xeyes`. Same probe (port
  as a 4th argument), run 3 times with identical results; reference frame from `vnc_snapshot.py` (raw encoding).
- **Our measurements on the Pi 5** (QNX 8.0 quick-start image 20260902, `vncserv` 4.2.0, native session):
  [notes/2026-11-12_spikes.md](notes/2026-11-12_spikes.md), [notes/2026-11-13_spike_rerun.md](notes/2026-11-13_spike_rerun.md)
  and the visible suite `tests/visible/suite.py` (run of 2026-11-14 00:28 UTC: 17 pass, 1 fail, 2 N/A).

## How to read the cells

| Mark | Meaning |
|---|---|
| **measured: works** | we ran it and checked the result; the test is named |
| **measured: fails** | we ran it and it failed; the test is named |
| by design | OculiX's code does this; **we have not run it** |
| no | the function does not exist on this path, by OculiX's code |
| not measured | nothing to go on yet |

Two limits of this version:

- **The "local host" column is not measured.** It is what OculiX's source and README say. We have only used OculiX
  against the Pi.
- **The "VNC, another computer" column is mostly not measured.** It is what `VNCScreen` and `VNCRobot` do by their
  code, for any server. Where a cell says "TigerVNC", it was measured read-only against TigerVNC 1.13.1 on UBUNTU24
  (see Sources); input against that server is not measured.

Where the QNX column says "with our VNC tool", the function was proved with `host/tools/vnc_input.py` or
`vnc_snapshot.py`, which send the same RFB messages as OculiX, but **not with OculiX itself**.

## The table

### Connection

| Function | Local host | VNC, another computer | VNC, QNX 8 | Gap and who closes it |
|---|---|---|---|---|
| Open a screen | `Screen` (all monitors) | `VNCScreen.start(host, port, password, ...)`, by design | **measured: works**, 3.3 s, O01 (OculiX finds the Gears tile and clicks it) | none |
| Authentication | not needed | VNC password only (release notes: "VncAuth only"), by design | **measured: works** with `vncserv -auth` | none; no encryption on the wire, see SSH tunnel |
| Encodings | not needed | Raw, ZRLE, Tight (release notes), by design; TigerVNC: **measured: fails**, OculiX's ZRLE decoder dies during start (`ZRLEDecoder: assertion (len <= end - ptr) failed`), `start` still returns a running screen and the frame is wrong in 72,666 pixels, all inside the windows ([upstream/oculix-zrle-decoder-tigervnc.md](upstream/oculix-zrle-decoder-tigervnc.md)) | `vncserv` serves raw and ZRLE (**measured** with our VNC tool); which one OculiX picked: not measured | measure |
| SSH tunnel | not needed | `SSHTunnel` class, by design | not measured (the image runs `sshd`) | measure |
| Several sessions at once | several `Screen`s | "thread-safe parallel sessions" (README), by design | not measured | measure |
| More than one display | yes | one framebuffer per server | `vncserv -display=1`: one display; a second HDMI display not tried | open |

### Seeing the screen

| Function | Local host | VNC, another computer | VNC, QNX 8 | Gap and who closes it |
|---|---|---|---|---|
| Capture the whole screen | yes | `capture()`, by design; TigerVNC: returns in 0.17 s, image type 1 (`TYPE_INT_RGB`; type 0 on the Pi), but **the content is wrong** (ZRLE defect above) | **measured: works**, 0.21 s for 1920 x 1080, O01 (OculiX finds the Gears tile and clicks it), U02 (unboxing: OculiX capture and OCR) | none |
| Capture a region | yes | `capture(x, y, w, h)`, by design | **measured: works** from UBUNTU24: 0.21 s for 400 x 300, identical to the same area of a full capture | none |
| Exact pixels | yes | depends on the server's pixel format | **measured:** `vncserv` reads back rgb565, so values are off by up to about 8 levels against Screen's own capture | ON agent: exact region capture (`screen_read_display()`); or `vncserv -format=rgba8888` |
| Colour at a point | `getColorAt` | by design (a 1 x 1 capture) | **measured: works** from UBUNTU24 (`Location.getColor`, 0.15 s, 5 of 5 points equal to the capture); subject to the rgb565 rounding above | as above |
| Pointer in the picture | yes | depends on the server | **measured:** the real pointer is not in the frames in the native session; `vncserv` draws an extra white pointer at (0,0) | avoid matching in the top-left 8 x 15 pixels |
| Is the picture live or frozen? | n/a | VNC cannot tell | **measured:** `vncserv` sends a full frame on every request, changed or not | **ON agent: frame freshness** (`screen_notify`) |
| Is the monitor on, blank or locked? | n/a | VNC cannot tell | **measured:** the monitor went black while VNC frames and Screen's power state stayed normal | **ON agent: status**; a camera sees it (C01 (camera sees the monitor)) |
| Frame rate | n/a | depends | **measured:** 5 to 6 frames a second (raw and ZRLE) | fine for step-and-check tests |

### Finding things

| Function | Local host | VNC, another computer | VNC, QNX 8 | Gap and who closes it |
|---|---|---|---|---|
| Find an image: `find`, `exists`, `has`, `wait`, `waitVanish`, `click(image)` | yes | by design; TigerVNC: `exists`, `find` **do not throw**, score 1.00 at the right place, but only because the pattern was cut from the same corrupt frame | **measured: fails** with `ClassCastException` in `Commons.getMatList` ([upstream/oculix-vncscreen-find-classcast.md](upstream/oculix-vncscreen-find-classcast.md)) | **OculiX fix** (David's fork, later); workaround below |
| The same through `Finder` on a copy of the capture | yes | by design | **measured: works**, score 0.99 to 1.00 in 0.1 to 0.8 s, O01 (OculiX finds the Gears tile and clicks it) | workaround in use |
| `findAll`, `findBest`, `findAny` | yes | by design; TigerVNC: `findAll` does not throw (same caveat) | `findAll` **measured: fails** from UBUNTU24, same `ClassCastException`; `findBest`, `findAny` not measured | OculiX fix; `Finder` workaround |
| Similarity tolerance | yes | yes | **measured** only with a pattern cut from the same VNC source; patterns from elsewhere not measured | measure; allow for the rgb565 rounding |
| Read text (OCR): `text`, `findText`, `existsText`, `waitText` | Tesseract bundled; PaddleOCR optional | by design; TigerVNC: `VNCScreen.text()`, `Region.text()`, `OCR.readText(raw capture)` do not throw (0.5 to 1.0 s), the text is useless because the frame is corrupt | on a BGR copy of the capture **measured: works**: 24 of 24 expected lines, U03 (unboxing: welcome text is right for this release); `VNCScreen.text()`, `Region.text()` on the VNC screen and `OCR.readText` on the raw capture **measured: fail** from UBUNTU24 with the same `ClassCastException`; on Linux OCR only loads with a workaround (see Host differences) | OculiX fix; workaround: OCR a BGR copy |
| Watch for change: `onAppear`, `onVanish`, `onChange`, `observe` | yes | by design (compares captures) | not measured; the image-based ones share the bug's code path | measure |
| Wait until the screen is stable: `waitForStable` | yes | `VNCScreen.waitForStable`, by design | `waitForScreenStable(3000, 500)` **measured: works** from UBUNTU24 (true after 1.16 s; deprecated in 4.0.0); `waitForStable(long, long, double)` not measured | the ON agent's freshness signal is the cheaper way |
| Highlight a region on screen | yes | **no**: skipped for remote screens (`Region.highlight`) | no | none needed |

### Mouse

| Function | Local host | VNC, another computer | VNC, QNX 8 | Gap and who closes it |
|---|---|---|---|---|
| Move the pointer, `hover` | yes | pointer events, smooth move, by design | **measured: works** with our VNC tool: Screen counted 41 of 41 moves, F02 (pointer moves); with OculiX: not measured | measure |
| Click at a position | yes | by design | **measured: works** with OculiX, O01 (OculiX finds the Gears tile and clicks it); and with our VNC tool, F03 (click opens an app) | none |
| Double click, right click, middle click | yes | buttons 1 to 3, by design | not measured: the launcher and the terminal give nothing to show it | needs an app that takes the mouse (our demo HMI) |
| Wheel | yes | buttons 4 and 5, by design | not measured | as above |
| Drag and drop | yes | by design | not measurable on this image: the terminal ignores the mouse, F06 (click puts the cursor), F07 (drag selects text), both N/A | as above |
| Touch | no | no | `vncserv -use_touch` turns buttons into touch contacts; not tried | open |

### Keyboard

| Function | Local host | VNC, another computer | VNC, QNX 8 | Gap and who closes it |
|---|---|---|---|---|
| Type text: `type`, `write` | yes | X keysyms, 2200+ defined, by design | **measured: works** with our VNC tool: F04 (keys reach the app), U01 (unboxing: log in); with OculiX: not measured | measure |
| Special keys (Enter, Backspace, arrows, F-keys) | yes | by design | Enter and Backspace **measured: work** with our VNC tool, F05 (Backspace); others not measured | measure |
| Modifiers and combinations, `keyDown`/`keyUp` | yes | by design | **measured: fails** for Alt+Del with our VNC tool: the app stays open, A02 (Alt+Del closes an app). Cause open: the launcher's config says "Alt+TAB+Del"; a real keyboard not tried | find the cause; app close is the ON agent's job anyway |
| Paste text, Unicode: `paste` | clipboard + Ctrl+V | **no**: `paste` fills the local clipboard and sends Ctrl+V; the client has no clipboard transfer to the server | no | type the text instead; or ON agent |
| Keyboard layouts | yes | keysyms, layout-independent, by design | not measured | measure |

### Around the screen

| Function | Local host | VNC, another computer | VNC, QNX 8 | Gap and who closes it |
|---|---|---|---|---|
| Start, focus and close applications: `App` | yes (Windows, macOS, Linux) | **no**: `App` acts on the local system | no | **ON agent / SSH:** start and close **measured: work** over SSH, A01 (camera viewfinder runs), A04 (Gears tile) |
| Clipboard: `App.getClipboard`/`setClipboard` | yes | **no**: local clipboard only | no | not needed so far |
| Run a command | yes (local) | no | no | **SSH** (the `on` command) |
| Know what is running, crashes, logs | local tools | no | no | **ON agent / SSH:** processes, core dumps and system log **measured** after every test |
| Put the target back in a known state | n/a | no | no | **SSH:** apps closed and the launcher back after each app test, **measured**, A04 (Gears tile); reboot **measured: works**, A03 (sudo shutdown returns to the launcher); TD01 (target back to its initial condition) has not had a clean run yet |
| Global hotkeys, recorder, capture prompt | yes | local to the host | local to the host | none needed |
| Android over ADB, MCP audit module, script runners | yes | not VNC | not VNC | out of scope |

## Host differences: Windows 11 and Ubuntu 24.04

The same read-only probe, `host/spikes/probe/ReadOnlyProbe.java` (written by the ON-UBUNTU24 session, run unchanged on
both hosts), against the same Pi showing the terminal with the welcome message, on 2026-11-14. It sends no pointer or
key events. Windows: ON-ASUSROG-win-11, `oculixapi-4.0.0-windows.jar`. Ubuntu: UBUNTU24 (x86_64, X11),
`oculixapi-4.0.0-linux.jar`, run by ON-UBUNTU24. Both Temurin 25.0.4+7.

| Function | Windows 11 | Ubuntu 24.04 |
|---|---|---|
| `VNCScreen.start` | works, 3.26 s | works, 3.45 to 3.48 s |
| `capture()`, 1920 x 1080 | works, 0.20 to 0.23 s; image type 0 (`DirectColorModel`, `DataBufferInt`) | works, 0.46 to 0.59 s; the same image type |
| `capture(100, 100, 400, 300)` | works, 0.17 s, 0 pixels differ from the full capture | works, 0.21 to 0.23 s, 0 pixels differ |
| `Location.getColor`, 5 points | works, 0.15 s each, all equal to the capture | works, 0.15 to 0.16 s, all equal |
| `exists`, `find`, `findAll` on the `VNCScreen` | **fail**, `ClassCastException` | **fail**, the same exception |
| `Finder` on a BGR copy | works, score 1.0000 at 0.99 and 0.80, 0.09 to 0.21 s | works, score 1.0000, 0.59 to 0.90 s |
| `OCR.readText` on a BGR copy | works out of the box, 2.6 s (first call, engine start included), 1528 characters, 33 lines | works **only with `LD_LIBRARY_PATH`** set to Legerix's folder (`~/.cache/legerix/5.5.0-8/linux-x86-64/`), 13.8 s, 32 lines. Without it `libtesseract.so.5` cannot find `libleptonica.so.6`, OculiX falls back to a `tesseract` program that is not installed, and every OCR call fails |
| `OCR.readText` on the raw capture, `VNCScreen.text()`, `Region.text()` | **fail**, `ClassCastException` | **fail**, the same exception |
| `waitForScreenStable(3000, 500)` | works, true after 0.70 s | works, true after 1.16 s |

So OculiX behaves the same on both hosts against QNX's `vncserv`, apart from two things: Linux needs the OCR library
workaround, and Windows is two to three times faster for captures, searches and OCR (different machines, so this is
not a fair speed comparison).

## What this says so far

1. **The path works end to end:** OculiX connects to QNX's `vncserv`, sees the screen, finds an image, clicks it and
   reads text. That is measured.
2. **One OculiX bug is in the way of the normal API:** image search and text reading called on the `VNCScreen` fail,
   on Windows and on Linux. The workaround (search a converted copy) is measured. **The bug depends on the server's
   pixel format:** against TigerVNC (32 bits per pixel) OculiX's capture is `TYPE_INT_RGB` and the same calls do not
   throw; against `vncserv` it is `TYPE_CUSTOM` and they do. `vncserv -format=rgba8888` might therefore avoid it;
   not tried (it is a target change).
3. **A second OculiX defect shows against TigerVNC, not against `vncserv`:** the ZRLE decoder fails and the frame is
   silently wrong. Against `vncserv` the captures matched our raw-encoding frames (region and colour checks, OCR of
   the welcome text), so either `vncserv` sends ZRLE that OculiX decodes, or it sends raw; which one is not measured.
   So far QNX's server is the one where OculiX's pixels can be trusted, and another server is the one where they
   cannot.
4. **Most of the QNX column is still "not measured" for OculiX itself.** Keys, moves and clicks are proved with our
   own VNC tools, which shows `vncserv` and Screen do their part, but not that OculiX's calls work.
5. **VNC cannot do four things, on any system, and those are the ON agent's job:** say whether the picture is
   fresh; say whether the display is on, blank or locked; start, stop and inspect applications; give exact pixels.

## What the ON agent must do (from the table)

| Need | Why VNC is not enough | Status |
|---|---|---|
| Frame freshness | `vncserv` resends the whole frame whether or not anything changed | signal proved: S01 (update signal fires on a change), S02 (no update signal on a static screen) |
| Display state: on, blank, locked | not in the VNC protocol; Screen's own state also missed a black monitor | open: cause of the black monitor not found |
| Application control: start, close, list | `App` is local; Alt+Del over VNC does not close an app | start and close proved over SSH |
| Target health: processes, core dumps, log | not in the VNC protocol | proved over SSH in the suite |
| Exact region capture | `vncserv` rounds to rgb565 | proved with `screen_read_display()` as root (spike S2) |
| Text entry beyond typed keys (Unicode, paste) | no clipboard transfer | not needed yet |

## Next measurements

1. **An OculiX probe on the Pi:** one program that calls each `VNCScreen` function (type, special keys, modifiers,
   hover, double and right click, wheel, drag, region capture, colour, `waitForStable`, observe, OCR methods) and
   checks each result over SSH and in the frames. It turns the "not measured" cells of the QNX column into
   measured ones. Mouse functions need an app that reacts to the mouse.
2. **The middle column, continued:** done read-only against TigerVNC 1.13.1 (the bug depends on the pixel format;
   a new ZRLE defect). Still to do: a server or setting where OculiX's captures are correct (x11vnc, wayvnc, or
   TigerVNC with ZRLE off on the server side), then input functions against it.
3. **Which encoding OculiX uses against `vncserv`:** from `vncserv`'s log or a packet capture.
4. **`vncserv -format=rgba8888`** on the Pi: does it remove the rounding and the bug? It is a recorded target
   change.
5. **Cross-host runs against real desktops** (planned with David, 2026-11-14). Each pair first with the read-only
   probe (no input, no coordination needed), then input tests in a slot David schedules, since input moves the
   mouse and types on that machine:

   | OculiX runs on → / VNC to ↓ | Windows 11 | Ubuntu 24.04 | macOS (MacBook M4 Pro) |
   |---|---|---|---|
   | QNX 8 (Pi 5) | measured | measured | later |
   | Ubuntu 24.04 desktop | planned | local column | later |
   | Windows 11 desktop | local column | planned | later |
   | macOS desktop | planned | planned | local column |

   - **Windows host → UBUNTU24's desktop:** UBUNTU24's desktop is Wayland, so the server is GNOME's own desktop
     sharing (if it offers VNC on this release) or x11vnc in an X11 session; the port opened to the LAN. ON-UBUNTU24
     checks what the release offers.
   - **UBUNTU24 → this Windows 11 laptop:** Windows has no VNC server built in; one is installed (TightVNC, UltraVNC
     or TigerVNC; David chooses) and recorded in [CONFIG_MANAGEMENT.md](CONFIG_MANAGEMENT.md), with a firewall rule.
   - **Both hosts → the MacBook M4 Pro:** macOS Screen Sharing (port 5900), with "VNC viewers may control screen
     with password" on, since OculiX only does VncAuth; that setting is recorded. A third real server next to
     TigerVNC and `vncserv`, and a Retina-size frame. The Mac is at 10.30.1.128 (2026-11-14: ports
     5900 and 22 refused, so Screen Sharing is off); macOS 27.2.1 (David).
   - **Local column:** the same probe on each host against its own `Screen`, without VNC.
   - **Later, after the QNX column is complete:** OculiX running on the Mac (QNX SDP 8 has no macOS host; Apple
     Silicon builds of OculiX's native libraries and OCR not checked yet) and input tests against the Mac.
