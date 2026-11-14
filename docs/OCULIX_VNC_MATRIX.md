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
| Encodings | not needed | Raw, ZRLE, Tight (release notes), by design; TigerVNC: **measured: fails**, OculiX's ZRLE decoder dies during start (`ZRLEDecoder: assertion (len <= end - ptr) failed`), `start` still returns a running screen and the frame is wrong in 72,666 pixels, all inside the windows ([upstream/oculix-zrle-decoder-tigervnc.md](upstream/oculix-zrle-decoder-tigervnc.md)) | `vncserv` serves raw and ZRLE (**measured** with our VNC tool); OculiX asks for ZRLE first and **receives only ZRLE**, at rgb565 16 bits per pixel, and decodes it correctly (**measured**, `host/spikes/probe/EncodingProbe.java`, 2026-11-14) | none; keep 16 bits per pixel (see the ZRLE note) |
| SSH tunnel | not needed | `SSHTunnel` class, by design | not measured (the image runs `sshd`) | measure |
| Several sessions at once | several `Screen`s | "thread-safe parallel sessions" (README), by design | not measured | measure |
| More than one display | yes | one framebuffer per server | `vncserv -display=1`: one display; a second HDMI display not tried | open |

### Seeing the screen

| Function | Local host | VNC, another computer | VNC, QNX 8 | Gap and who closes it |
|---|---|---|---|---|
| Capture the whole screen | yes | `capture()`, by design; TigerVNC: returns in 0.17 s, image type 1 (`TYPE_INT_RGB`; type 0 on the Pi), but **the content is wrong** (ZRLE defect above) | **measured: works**, 0.21 s for 1920 x 1080, O01 (OculiX finds the Gears tile and clicks it), U02 (unboxing: OculiX capture and OCR) | none |
| Capture a region | yes | `capture(x, y, w, h)`, by design | **measured: works** from UBUNTU24: 0.21 s for 400 x 300, identical to the same area of a full capture | none |
| Exact pixels | yes | depends on the server's pixel format; TigerVNC at depth 16: **measured: OculiX decodes rgb565 colours wrongly** (background (32,64,98) becomes (33,66,49)) | **measured:** `vncserv` reads back rgb565, so values are off by up to about 8 levels against Screen's own capture; and **OculiX then decodes rgb565 wrongly**: 75,919 of 2,073,600 pixels differ from a raw-encoding frame of the same, unchanged screen, from Windows and from UBUNTU24 alike, for example (49,4,0) becomes (49,0,132) and (230,231,230) becomes (230,230,247); red is right, green and blue are wrong ([upstream/oculix-rgb565-colours.md](upstream/oculix-rgb565-colours.md)) | ON agent: exact region capture (`screen_read_display()`); or `vncserv -format=rgba8888` |
| Colour at a point | `getColorAt` | by design (a 1 x 1 capture) | **measured: wrong colours**: `Location.getColor` (0.15 s) equals OculiX's own capture at 5 of 5 points, but the capture's colours are wrong (row above); only black, white and some other colours come through right | OculiX fix; meanwhile colours from the ON agent or our raw-encoding VNC tool |
| Pointer in the picture | yes | depends on the server | **measured:** the real pointer is not in the frames in the native session; `vncserv` draws an extra white pointer at (0,0) | avoid matching in the top-left 8 x 15 pixels |
| Is the picture live or frozen? | n/a | VNC cannot tell | **measured:** `vncserv` sends a full frame on every request, changed or not | **ON agent: frame freshness** (`screen_notify`) |
| Is the monitor on, blank or locked? | n/a | VNC cannot tell | **measured:** the monitor went black while VNC frames and Screen's power state stayed normal | **ON agent: status**; a camera sees it (C01 (camera sees the monitor)) |
| Frame rate | n/a | depends | **measured:** 5 to 6 frames a second (raw and ZRLE) | fine for step-and-check tests |

### Finding things

| Function | Local host | VNC, another computer | VNC, QNX 8 | Gap and who closes it |
|---|---|---|---|---|
| Find an image: `find`, `exists`, `has`, `wait`, `waitVanish`, `click(image)` | yes | by design; TigerVNC: `exists`, `find` **do not throw**, score 1.00 at the right place, but only because the pattern was cut from the same corrupt frame | **measured: fails** with `ClassCastException` in `Commons.getMatList` ([upstream/oculix-vncscreen-find-classcast.md](upstream/oculix-vncscreen-find-classcast.md)) | **OculiX fix** (David's fork, later); workaround below |
| The same through `Finder` on a copy of the capture | yes | by design | **measured: works**, score 0.99 to 1.00 in 0.1 to 0.8 s, O01 (OculiX finds the Gears tile and clicks it); patterns must be cut from OculiX's own captures, since its colours are wrong (see Exact pixels); `Finder` needs a local display even for an image (headless Java fails) | workaround in use |
| `findAll`, `findBest`, `findAny` | yes | by design; TigerVNC: `findAll` does not throw (same caveat) | `findAll` **measured: fails** from UBUNTU24, same `ClassCastException`; `findBest`, `findAny` not measured | OculiX fix; `Finder` workaround |
| Similarity tolerance | yes | yes | **measured** only with a pattern cut from the same VNC source; patterns from elsewhere not measured | measure; allow for the rgb565 rounding |
| Read text (OCR): `text`, `findText`, `existsText`, `waitText` | Tesseract bundled; PaddleOCR optional | by design; TigerVNC: `VNCScreen.text()`, `Region.text()`, `OCR.readText(raw capture)` do not throw (0.5 to 1.0 s), the text is useless because the frame is corrupt | on a BGR copy of the capture **measured: works**: 24 of 24 expected lines, U03 (unboxing: welcome text is right for this release); `VNCScreen.text()`, `Region.text()` on the VNC screen and `OCR.readText` on the raw capture **measured: fail** from UBUNTU24 with the same `ClassCastException`; on Linux OCR only loads with a workaround (see Host differences) | OculiX fix; workaround: OCR a BGR copy |
| Watch for change: `onAppear`, `onVanish`, `onChange`, `observe` | yes | by design (compares captures) | not measured; the image-based ones share the bug's code path | measure |
| Wait until the screen is stable: `waitForStable` | yes | `VNCScreen.waitForStable`, by design | `waitForScreenStable(3000, 500)` **measured: works** from UBUNTU24 (true after 1.16 s; deprecated in 4.0.0); `waitForStable(long, long, double)` not measured | the ON agent's freshness signal is the cheaper way |
| Highlight a region on screen | yes | **no**: skipped for remote screens (`Region.highlight`) | no | none needed |

### Mouse

| Function | Local host | VNC, another computer | VNC, QNX 8 | Gap and who closes it |
|---|---|---|---|---|
| Move the pointer, `hover` | yes | TigerVNC depth 16 (ON-UBUNTU24, 2026-11-14, sent and received checked): **measured: works**, `hover` and `mouseMove` send 6 interpolated moves in 0.5 s; the first move of a session starts at 0,0 | OculiX on the Pi (ON-UBUNTU24, ON-VNC-1, 2026-11-14): **measured: works**, 68 of 68 pointer events reached Screen (`screen_probe` counter); no hover highlight on the launcher and the pointer is not drawn in the VNC frame, so nothing shows on screen; with our VNC tool: 41 of 41, F02 (pointer moves) | measure |
| Click at a position | yes | TigerVNC depth 16 (ON-UBUNTU24, 2026-11-14, sent and received checked): **measured: works** (button mask 1) | OculiX on the Pi (ON-UBUNTU24, ON-VNC-1, 2026-11-14): **measured: works**, `click` on the Terminal tile opens it; also O01 (OculiX finds the Gears tile and clicks it) and, with our VNC tool, F03 (click opens an app) | none |
| Double click, right click, middle click | yes | TigerVNC depth 16 (ON-UBUNTU24, 2026-11-14, sent and received checked): `doubleClick` and `rightClick` **measured: work**; `mouseDown(Button.LEFT or MIDDLE)` + `mouseUp()` **measured: fails**, only moves with no button are sent, yet the call returns success | OculiX on the Pi (ON-UBUNTU24, ON-VNC-1, 2026-11-14): `rightClick`, `mouseDown`/`mouseUp` (masks 1, 3, 7, 0) sent as expected, no visible effect on the launcher background; double click N/A (it might launch a tile twice) | needs an app that takes the mouse (our demo HMI) |
| Wheel | yes | TigerVNC depth 16 (ON-UBUNTU24, 2026-11-14, sent and received checked): **measured: works**, down mask 16 (X button 5), up mask 8 (X button 4) | N/A on this image: nothing to scroll on the launcher or in the terminal | as above |
| Drag and drop | yes | TigerVNC depth 16 (ON-UBUNTU24, 2026-11-14, sent and received checked): `dragDrop` and `drag` + `dropAt` **measured: work** (press, 6 moves with the button held, release) | OculiX on the Pi (ON-UBUNTU24, ON-VNC-1, 2026-11-14): `dragDrop` sent as expected, no visible effect on the launcher; in the terminal not measurable (it ignores the mouse), F06 (click puts the cursor), F07 (drag selects text), both N/A | as above |
| Touch | no | no | `vncserv -use_touch` turns buttons into touch contacts; not tried | open |

### Keyboard

| Function | Local host | VNC, another computer | VNC, QNX 8 | Gap and who closes it |
|---|---|---|---|---|
| Type text: `type`, `write` | yes | TigerVNC depth 16 (ON-UBUNTU24, 2026-11-14, sent and received checked): **measured: works** for lowercase, uppercase, digits and all ASCII punctuation (Shift wrapped around shifted characters); `write("ab#ENTER.")` works | OculiX on the Pi (ON-UBUNTU24, ON-VNC-1, 2026-11-14): **measured: works**: login with `type("qnxuser\r")`, lowercase, uppercase, digits, space and all ASCII punctuation arrive exactly; `write("ab#ENTER.")` works; the launcher also takes keys (typed text selected a tile and Return launched it); with our VNC tool: F04 (keys reach the app), U01 (unboxing: log in) | measure |
| Special keys (Enter, Backspace, arrows, F-keys) | yes | TigerVNC depth 16 (ON-UBUNTU24, 2026-11-14, sent and received checked): `type("\r")` sends Return and works; Tab, Esc, Backspace, Space, arrows, Home, End, Page Up, Insert, F5 work; **`Key.ENTER` sends Linefeed (0xff0a), not Return**; **`Key.DELETE` sends KP_Delete (0xff9f), not Delete** | OculiX on the Pi (ON-UBUNTU24, ON-VNC-1, 2026-11-14): `type("\r")` (Return) **works**; **`Key.ENTER` (Linefeed) does nothing on QNX**; Tab works; **Backspace arrives as ^H and does not erase under `cat -v`** (with our VNC tool it erased at the login prompt, F05 (Backspace), so this depends on the terminal mode; to check); arrows, Home, End, Page Up, Insert, F5 arrive as their escape sequences; Esc shows nothing; **`Key.DELETE` (KP_Delete) arrives as "."** | measure |
| Modifiers and combinations, `keyDown`/`keyUp` | yes | TigerVNC depth 16 (ON-UBUNTU24, 2026-11-14, sent and received checked): Ctrl, Ctrl+Alt, Alt+Tab, `keyDown(Key.CTRL)`/`type`/`keyUp`, Shift + click **measured: work**; **Ctrl+Shift+letter sends the lowercase keysym with Shift held**, and the TigerVNC server answers with Caps Lock, which stays on afterwards; **`KeyModifier.META` sends Meta_L, not Super** (TigerVNC turned it into Alt) | OculiX on the Pi (ON-UBUNTU24, ON-VNC-1, 2026-11-14): Ctrl+letter and `keyDown(Key.CTRL)`/`type`/`keyUp` **work** (^A, ^B, Ctrl+D); held Shift works ("abc1" gives "ABC!"); **Ctrl+Shift+letter and Ctrl+Alt+letter arrive as ^@ (NUL)**, no Caps Lock left on; Alt+letter and `KeyModifier.META` + letter arrive as the plain letter; Alt+Del with our VNC tool does not close an app, A02 (Alt+Del closes an app) | find the cause; app close is the ON agent's job anyway |
| Paste text, Unicode: `paste` | clipboard + Ctrl+V | **measured: no** (TigerVNC depth 16 (ON-UBUNTU24, 2026-11-14, sent and received checked)): `paste()` and `type()` with a non-ASCII character send only Ctrl+V, no clipboard message, the text never arrives, and the call returns 1; by design `paste` fills the local clipboard and sends Ctrl+V; the client has no clipboard transfer to the server | **measured: no** (OculiX on the Pi (ON-UBUNTU24, ON-VNC-1, 2026-11-14)): `type("é")` and `paste()` send only Ctrl+V and nothing arrives | type the text instead; or ON agent |
| Keyboard layouts | yes | keysyms, layout-independent, by design | not measured | measure |

Input was measured by ON-UBUNTU24 against its private TigerVNC display at depth 16 (not the Pi), with an event
viewer on the display (what the X server received) and an RFB proxy in between (what OculiX sent): Ubuntu 24.04,
Temurin 25.0.4+7, 38 steps, one run each with 4.0.0 alone and with the 2.0.4 jar first; **both send identical
input**. Every call returns success, including those that delivered nothing. For the Pi this means: send Enter as
`type("\r")`, Delete as the Delete keysym, avoid `mouseDown`/`mouseUp`, and check Ctrl+Shift combinations and
Caps Lock there (QNX not measured). Input on the Pi waits for a slot David schedules.

**On the Pi** (ON-UBUNTU24, 2026-11-14, 04:3x to 04:46 UTC; ON-VNC-1 with `oculixapi-4.0.0-linux.jar`, Temurin
25.0.4+7, through an RFB proxy that logs what OculiX sent; one run): pointer phase on the launcher, keyboard phase in
st under `cat -v`, then `sudo shutdown` back to the launcher (0 pixels different from the frame before, no app left
open, no new core dump). The Pi had rebooted by itself at 04:33:15 UTC before the slot (cause unknown). For the ON
agent and the suite: send Enter as `type("\r")`, Delete as the Delete keysym (0xffff), avoid Ctrl+Shift and
Ctrl+Alt combinations through OculiX, and do not expect to see the pointer in VNC frames.

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
| `Location.getColor`, 5 points | equal to the capture, 0.15 s each; capture colours wrong (Exact pixels) | the same, 0.15 to 0.16 s |
| `exists`, `find`, `findAll` on the `VNCScreen` | **fail**, `ClassCastException` | **fail**, the same exception |
| `Finder` on a BGR copy | works, score 1.0000 at 0.99 and 0.80, 0.09 to 0.21 s | works, score 1.0000, 0.59 to 0.90 s |
| `OCR.readText` on a BGR copy | works out of the box, 2.6 s (first call, engine start included), 1528 characters, 33 lines | works **only with `LD_LIBRARY_PATH`** set to Legerix's folder (`~/.cache/legerix/5.5.0-8/linux-x86-64/`), 13.8 s, 32 lines. Without it `libtesseract.so.5` cannot find `libleptonica.so.6`, OculiX falls back to a `tesseract` program that is not installed, and every OCR call fails |
| `OCR.readText` on the raw capture, `VNCScreen.text()`, `Region.text()` | **fail**, `ClassCastException` | **fail**, the same exception |
| `waitForScreenStable(3000, 500)` | works, true after 0.70 s | works, true after 1.16 s |

So OculiX behaves the same on both hosts against QNX's `vncserv`, apart from two things: Linux needs the OCR library
workaround, and Windows is two to three times faster for captures, searches and OCR (different machines, so this is
not a fair speed comparison).

## Newer VNC client library: tigervnc-java-oculix 2.0.4 first on the classpath

Asked for by the Oculix-MacBookM4Pro-macOS-27 session; run read-only by ON-UBUNTU24 (arms A = 4.0.0 alone,
B = 2.0.2 first, C = 2.0.4 first) and arm C repeated from Windows, 2026-11-14. Jars from Maven Central
(`io.github.oculix-org:tigervnc-java-oculix`), SHA-256 2.0.2 `3ec287d9...6698`, 2.0.4 `de6fc7ca...24ca6`;
`-verbose:class` confirmed the override loads. **Most of the defects above go away with 2.0.4:**

| Server | A: 4.0.0 alone | B: 2.0.2 first | C: 2.0.4 first |
|---|---|---|---|
| QNX `vncserv` (Pi), from UBUNTU24 | 75,919 wrong pixels, `ClassCastException` | the same | **0 wrong pixels**, find, `findAll`, `text()` and `getColor` right, no exception; capture 0.17 to 0.25 s (A: 0.46 to 0.58 s) |
| QNX `vncserv` (Pi), from Windows | 75,919 wrong pixels, `ClassCastException` | not run | **0 wrong pixels** against two identical raw frames; `exists`, `find` at the right place (score 1.00), `findAll` 1 match, `OCR.readText` on the raw capture and `VNCScreen.text()` 1528 characters, `Region.text()` works; capture 0.15 to 0.19 s |
| TigerVNC, depth 24 | 72,666 wrong | 0 wrong | 0 wrong |
| x11vnc | frame black (both the `:135` and `:85` errors seen in different runs) | correct except the 94-pixel pointer | the same as B |
| TigerVNC, depth 16 | 1,757,793 wrong, `ClassCastException` | the same | 0 wrong, find and OCR work |
| macOS Screen Sharing, from Windows | frame all black | not run | **login fails**: `AuthFailureException` straight after the 3.8 handshake, with the password that works for 4.0.0 and for our raw tool; which security type 2.0.4 picks from Apple's list (30, 33, 36, 31, 32, 2, 35) not identified; not retried, to avoid locking the account. **Cause confirmed** (Oculix-MacBookM4Pro-macOS-27 session, 2026-11-13, offline: a fake RFB 3.8 server offering the Mac's exact list records the client's choice and closes, so no login and no lockout risk; `fleet/repro-2026-11-13-vnc-sectype/` in David's OculiX repo): 4.0.0 alone chooses 2 (VncAuth); 2.0.4 first chooses 30 (Apple DH, which needs the macOS account name and password), because 2.0.4's `CConnection.processSecurityTypesMsg` takes the first type in the server's order that it supports; 2.0.4 with `SecurityClient.secTypes.setParam("VncAuth,None")` before `VNCScreen.start` chooses 2 again. A full login with that setting is not tested and waits for David. QNX's `vncserv` offers only VncAuth, so it is not affected. An OculiX built on 2.0.4 needs an OculiX-side change (limit the security types when no user name is given, or take a user name) before it is safe for Mac targets |

Why (Oculix session, from the source; seen in the TigerVNC depth 16 server log): 2.0.2 fixes the 32 bpp ZRLE decoder;
2.0.4 also asks every server for 32 bpp rgb888, so the rgb565 colour model and the `TYPE_CUSTOM` image are never
used. The colour-model and `getMatList` defects are still in the code, only not reached.

Also seen: `Location.getColor` and `capture(x, y, w, h)` send update requests sized (w - x, h - y), wrapped to 16
bits (for example 65527 x 65527 at 10,10): `refreshFramebuffer` passes width and height where `Rect` takes corners
(`VNCClient.java:167`, still in 2.0.4). The server clips them, so results are right; partial captures came back
stale in none of 126 rounds (ON-UBUNTU24, its local display).

**Acceptance checks of the proposed test configuration "ON-VNC-1"** (from the Oculix-MacBookM4Pro-macOS-27 session:
2.0.4 first on the classpath, hash-verified jars, security types limited to `VncAuth,None`, Enter as `type("
")`),
Windows host to the Pi, 2026-11-14, Eclipse Adoptium 25.0.4+7-LTS, read-only: both jars' SHA-256 match;
`-verbose:class` loads `com.sikulix.vnc.VNCClient` and `com.tigervnc.rfb.ZRLEDecoder` from the 2.0.4 jar; the log
says "Choosing security type VncAuth(2)"; no `Thread-N` exception; capture type 1; 0 wrong pixels against two
identical raw frames. All five pass. **Adopted by David for the suite and all OculiX testing on 2026-11-14.** From UBUNTU24 (ON-UBUNTU24) the same checks on the Pi
pass with Temurin 25.0.4+7 and with the Temurin 17.0.21+9 spot-check (0 wrong pixels, type 1, VncAuth(2)).

Not tested with 2.0.4: any input, more than one run, the Mac login failure's cause. It is a classpath override of a
pinned release, recorded in CONFIG_MANAGEMENT.md as a trial host component; the suite does not use it yet.

## What this says so far

1. **The path works end to end:** OculiX connects to QNX's `vncserv`, sees the screen, finds an image, clicks it and
   reads text. That is measured.
2. **One OculiX bug is in the way of the normal API:** image search and text reading called on the `VNCScreen` fail,
   on Windows and on Linux. The workaround (search a converted copy) is measured. **The bug depends on the server's
   pixel format:** against TigerVNC (32 bits per pixel) OculiX's capture is `TYPE_INT_RGB` and the same calls do not
   throw; against `vncserv` it is `TYPE_CUSTOM` and they do. `vncserv -format=rgba8888` might therefore avoid it;
   not tried (it is a target change).
3. **A second OculiX defect: ZRLE at 32 bits per pixel.** Against TigerVNC 1.13.1 and x11vnc 0.9.16 (both 32 bits
   per pixel, depth 24) OculiX's ZRLE decoder dies during start (TigerVNC: length assertion, `ZRLEDecoder.java:135`;
   x11vnc: `ZlibInStream: inflate failed` at `ZRLEDecoder.java:85`) and the frame is silently wrong (x11vnc: 2,041,346
   of 2,073,600 pixels). Against `vncserv` OculiX also receives ZRLE, but at 16 bits per pixel, and decodes it
   correctly. ZRLE sends 3-byte "compact pixels" only at 32 bits per pixel, so that is the likely fault (OculiX issue
   #443, "ZRLE CPIXEL", is in the same place); not proved. OculiX has no setting to ask for another encoding
   (`VNCClient.currentEncoding` is private and sent once). **So `vncserv -format=rgba8888` would probably trade the
   `ClassCastException` for corrupt frames.**
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

Points from the "Oculix-MacBookM4Pro-macOS-27" session (working in David's OculiX fork, relayed by ON-MacBookM4Pro,
2026-11-13), **read from the 4.0.0 source, not run**, to use in the input probes:

- 4.0.0's VNC client (tigervnc-java-oculix 2.0.1) offers only VncAuth and None security.
- ZRLE is hard-coded. Issue #443 ("ZlibInStream: inflate failed" at 32 bpp) is fixed in tigervnc-java 2.0.2 and
  later, which no OculiX release contains. Our x11vnc failure has that exact message; TigerVNC and macOS fail
  differently (`ZRLEDecoder.java:135` assertion), so #443 covers those two only if one fix covers both. Not proved.
- A decoder exception kills the reader thread and the script carries on, so a pass or a `FindFailed` may come from a
  dead framebuffer. **Every probe run checks stderr for `Thread-N` exceptions and keeps one capture.**
- `Key.ENTER` sends XK_Linefeed, not Return: input probes try both `type("
")` and `Key.ENTER`.
- `type()` with non-ASCII text pastes from the local clipboard with the local hotkey, so expect it to fail over VNC.
- Colour-mapped (palette) servers are not supported; 16 bpp has its own code path.

1. **An OculiX probe on the Pi:** one program that calls each `VNCScreen` function (type, special keys, modifiers,
   hover, double and right click, wheel, drag, region capture, colour, `waitForStable`, observe, OCR methods) and
   checks each result over SSH and in the frames. It turns the "not measured" cells of the QNX column into
   measured ones. Mouse functions need an app that reacts to the mouse.
2. **The middle column, continued:** done read-only against TigerVNC 1.13.1 and x11vnc 0.9.16 (both corrupt through
   ZRLE at 32 bits per pixel). Still to do: a server set to 16 bits per pixel, or with ZRLE off, to confirm the
   cause and get correct captures, then input functions against it. **Done for TigerVNC at depth 16**
   (ON-UBUNTU24, 2026-11-14): ZRLE decodes without error, which supports the 3-byte pixel cause (x11vnc at depth 16
   not run); the capture is `TYPE_CUSTOM` and the `ClassCastException` reproduces, so that bug follows the 16-bit
   format, not QNX; and colours are decoded wrongly, as on the Pi. So **no server tried gives OculiX 4.0.0 a fully
   correct screen**, and TigerVNC at depth 16 reproduces the Pi's behaviour without a Pi.
3. **Which encoding OculiX uses against `vncserv`:** done, ZRLE at 16 bits per pixel (`EncodingProbe.java`).
4. **`vncserv -format=rgba8888`** on the Pi: does it remove the rounding and the bug? It is a recorded target
   change. Expect ZRLE corruption at 32 bits per pixel (point 3 above); try only after the cause is confirmed.
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
     5900 and 22 refused, so Screen Sharing is off); macOS 27.2.1 (David). Read-only facts from the
     ON-MacBookM4Pro session (2026-11-13): build 26B131, Apple M4 Pro; one display, the built-in Liquid Retina XDR,
     native 3024 x 1964, set to "looks like" 1352 x 878 at 2x, so the backing framebuffer is 2704 x 1756; the VNC
     frame may be either size, to be recorded from what OculiX sees. Application firewall off, stealth mode off.
     Screen Sharing turned on by David on 2026-11-13 (recorded in CONFIG_MANAGEMENT.md); the server offers VncAuth.
     **Read-only probe from Windows, 2026-11-14:** connects in 3.3 s (Apple's `RFB 003.889` negotiated down to 3.8,
     VncAuth works); framebuffer 2704 x 1756, 32 bpp depth 24; OculiX receives only ZRLE and its decoder dies with
     the same assertion as against TigerVNC (`ZRLEDecoder.java:135`). Every call then "works" on a frame that is
     **100% black** (the raw-encoding frame of the same screen is 36.7% black): `getColor` black at 5 of 5 points,
     `exists`/`find` score 1.00 at (0,0), `findAll` 543 matches, OCR 19 characters of noise. No `ClassCastException`
     (image type 1). So macOS Screen Sharing is the third 32 bpp server where OculiX 4.0.0 cannot see the screen.
     **The same from UBUNTU24** (ON-UBUNTU24, about 22:33 EST): start 3.36 s, the same assertion; captures 0.18 to
     0.27 s, 4,747,183 of 4,748,224 pixels black; `findAll` 549 matches in 9.76 s; the raw-encoding frame correct in
     2.3 s. Both hosts give the same result.
   - **Local column:** the same probe on each host against its own `Screen`, without VNC.
   - **First pass, decided 2026-11-14:** a free server per host with the same settings (VNC password, port 5900,
     the real desktop): macOS Screen Sharing (in place), a free Windows server (to choose), and on UBUNTU24 x11vnc on the real
     desktop. UBUNTU24, checked read-only by ON-UBUNTU24 on 2026-11-14: the session is X11 (display :1, laptop panel
     1366 x 768, depth 24; GNOME Shell 46.0); gnome-remote-desktop 46.2 offers RDP only (no VNC backend), so the route
     is x11vnc 0.9.16-10 (libvncserver 0.9.14) on :1, port 5900, VncAuth, started by hand for the runs: no reboot, no
     GDM change, nothing in /etc; ufw is not filtering. It serves 32 bpp, so it needs ON-VNC-1 (decoded correctly in
     the earlier tests, apart from the pointer, which x11vnc draws into the frame). Input against it drives David's
     real desktop on UBUNTU24, so input runs need a slot. Waiting for David's OK in ON-UBUNTU24's session. RealVNC Premium later for a retest on one product.
   - **One VNC server on all test hosts** (David, 2026-11-14, docs/HOW_WE_WORK.md). The only common product found
     is RealVNC Server (Windows, macOS, Linux). Checked from RealVNC's documentation and web sources, 2026-11-14,
     nothing installed:
     1. OculiX's login: RealVNC calls VncAuth "VNC Password"; non-RealVNC viewers need encryption set to PreferOn or
        PreferOff and the "Legacy" box ticked when the password is set (Linux: `vncpasswd -legacy -service`); then
        only 8 characters count and the session is not encrypted.
     2. Licence: the free Lite plan is non-commercial, 1 user, 3 devices, one connection at a time, one
        authentication scheme (which one not found). Direct LAN connections, which OculiX needs (it has no RealVNC
        cloud connection), are reported as starting at the Premium plan (about USD 30 a month); not confirmed on
        RealVNC's own comparison page. **Official pages, checked 2026-11-14 (search snippets; the pages refuse
        automated fetches):** the pricing page lists "direct connections on LAN" under **Premium** (150 devices,
        5 active connections), not under Essentials (3 devices, 1 connection) or Plus (50 devices, 3 connections);
        Lite and Essentials have only the VNC Device Password scheme and must not change the Authentication
        parameter; cloud-only subscriptions time out on a direct connection; VncAuth is the only scheme a
        non-RealVNC viewer can use, and only over a direct connection. So OculiX needs **Premium** (prices quoted
        from "about GBP 10.75" to USD 29.75 a month; the exact figure is on the pricing page) or Enterprise.
     3. macOS: RealVNC's download page says it is working on a bug affecting RealVNC Server on macOS Tahoe (26);
        macOS 27 not mentioned.
     4. Ubuntu: RealVNC Server does not support Wayland; GDM has to be set to X11 (`WaylandEnable=false` in
        `/etc/gdm3/custom.conf`, then reboot), a recorded change on UBUNTU24.
   - **Later, after the QNX column is complete:** OculiX running on the Mac (QNX SDP 8 has no macOS host) and input
     tests against the Mac. The release has one macOS jar, `oculixapi-4.0.0-macos.jar` (61,441,991 bytes, SHA-256
     `e26cc3da...4bd3`), whose native libraries (OpenCV 4.10, Tesseract 5, Leptonica 6, JNA, JNativeHook) are all
     arm64; even its plain `darwin/` folder is arm64, so Intel Macs look unsupported. By their load commands,
     Tesseract and Leptonica link to absolute Homebrew paths (`/opt/homebrew/opt/{libpng,jpeg-turbo,libtiff}`) and
     to a CI runner path for Leptonica, so OCR on a Mac without Homebrew will probably fail to load, the same kind
     of problem as the Linux OCR load issue. Not run yet (ON-MacBookM4Pro, listing only).
