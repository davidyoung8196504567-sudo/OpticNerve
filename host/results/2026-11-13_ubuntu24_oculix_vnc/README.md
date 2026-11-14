# OculiX 4.0.0 over VNC, measured from UBUNTU24 (2026-11-13 / 14 UTC)

Raw results, logs, captures and probe sources of the ON-UBUNTU24 session. The conclusions are in
[docs/OCULIX_VNC_MATRIX.md](../../../docs/OCULIX_VNC_MATRIX.md) and [docs/upstream/](../../../docs/upstream/), kept
by the ON-ASUSROG-win-11 session; this folder is the evidence behind the Ubuntu rows. Shared at David's request.
No passwords are stored here: programs take them as arguments or from the environment, and the keysyms of the Pi
login password are removed from the tap log of run 10.

## Client

- UBUNTU24, 10.30.1.132, Ubuntu 24.04 x86_64, glibc 2.39, X11 session.
- `oculixapi-4.0.0-linux.jar`, SHA-256 `bfebf7841eeca171e632739c00eeba879683c32d7530e9af624ae447010c9554`.
- `tigervnc-java-oculix` from Maven Central: 2.0.2 `3ec287d950b395845f5cad8446d17fb92ee1cd742fb423242bad1c568abc6698`,
  2.0.4 `de6fc7ca87d9fd704b2b1b2d172f19e55c34f53cadd7e664739315ac15224ca6`.
- Temurin 25.0.4+7 (tarball SHA-256 `e58fcdcd637b25c03ca84cbbcefc70d11efb8f4b4cbd05decc9f661769d77f94`); Temurin
  17.0.21+9 (`8b9b611d917e3dfd8419d3566b5949226f69b052fed7e109c1368add1dfe8711`) for the spot-check in run 09.
- All of it unpacked under `~/opticnerve-work/`, nothing installed system-wide.
- Linux OCR needs `LD_LIBRARY_PATH=$HOME/.cache/legerix/5.5.0-8/linux-x86-64` (Legerix#26).
- "Arm A" = oculixapi alone. "Arm B" = the 2.0.2 jar first on the classpath. "Arm C" = the 2.0.4 jar first
  (the ON-VNC-1 configuration). The stdout files keep the two `-verbose:class` lines that show which jar
  `VNCClient` and `ZRLEDecoder` were loaded from; the other class-loading lines are removed.

## Servers

| Server | Where | Pixel format |
|---|---|---|
| QNX 8.0 vncserv 4.2.0, Pi 5, quick-start image 20260902 | 10.30.1.116:5900 | 16 bpp rgb565 |
| TigerVNC Xtigervnc 1.13.1+dfsg-2build2 | UBUNTU24, private display :5, localhost | depth 24 (32 bpp) or depth 16 (rgb565) |
| x11vnc 0.9.16-10, libvncserver1 0.9.14+dfsg-1ubuntu0.2 | UBUNTU24, exporting the same display | 32 bpp depth 24 |
| macOS Screen Sharing, MacBook M4 Pro | 10.30.1.128:5900 | 32 bpp depth 24, 2704x1756 |

The TigerVNC and x11vnc packages were unpacked from the Ubuntu noble `.deb` files without root. The local test
screen is a solid `#204060` root window, an `xmessage` window with three lines of text, and `xeyes`.
"Raw snapshot" means a frame taken with `host/tools/vnc_snapshot.py` (raw encoding), the reference every OculiX
capture is compared with.

## Runs

| Folder | What | Result |
|---|---|---|
| `runs/01_pi_readonly_4.0.0` | Read-only probe, arm A, Pi | Captures decode; type 0 image; `ClassCastException` in `Commons.getMatList` on find / exists / findAll / text(); Finder and OCR on a BGR copy work; 75,919 of 2,073,600 pixels have wrong colours |
| `runs/02_tigervnc_depth24_4.0.0` | Same, TigerVNC depth 24 | `ZRLEDecoder: assertion (len <= end - ptr) failed`; 72,666 wrong pixels; no `ClassCastException` (type 1) |
| `runs/03_x11vnc_4.0.0` | Same, x11vnc | `ZlibInStream: inflate failed` in the same decoder; 2,041,346 wrong pixels |
| `runs/04_tigervnc_depth16_4.0.0` | Same, TigerVNC depth 16 | Decodes; reproduces the Pi's `ClassCastException` and colour error (1,757,793 wrong pixels: background `(32,64,98)` read as `(33,66,49)`) |
| `runs/05_macos_screen_sharing_4.0.0` | Same, macOS | RFB 3.889 negotiated to 3.8, VncAuth accepted, 2704x1756; the same ZRLE assertion; 4,747,183 of 4,748,224 pixels black. The raw snapshots show the Mac's lock screen |
| `runs/06_abc_classpath` | Arms A / B / C against TigerVNC depth 24, x11vnc, TigerVNC depth 16, and B / C against the Pi | 2.0.2 fixes the 32 bpp ZRLE failure (0 wrong pixels). 2.0.4 also asks the server for 32 bpp rgb888, which removes the colour error and the `ClassCastException`: 0 wrong pixels on TigerVNC depth 16 and on the Pi, find and text() work |
| `runs/07_input_tigervnc_depth16` | Input functions, arms A and C, 38 steps; `mouseDown` constants; stuck-button recovery | See "Input" below. A and C send identical input |
| `runs/08_subregion` | Can `capture(x,y,w,h)` return stale pixels? 7 regions x 6 rounds, arm A depth 16, arm C depth 16 and 24 | 0 stale pixels in every round, although the update requests have the wrong size (`Rect(x,y,w,h)` taken as corners) |
| `runs/09_onvnc1_acceptance_pi` | ON-VNC-1 acceptance, read-only, Pi on its launcher, Temurin 25.0.4+7 and 17.0.21+9 | Both: classes from the 2.0.4 jar, "Choosing security type VncAuth(2)", no thread exception, image type 1, 0 wrong pixels |
| `runs/10_pi_input` | Input functions on the Pi, ON-VNC-1, in a slot agreed with ON-ASUSROG-win-11 | See "Input on the Pi" below |

In each read-only run, `full.png` is OculiX's first capture, `pattern.png` the pattern cut from it, `region.png`
the sub-region capture and `ocr_*.txt` the OCR text. Runs 02 to 04 were repeated two or three times with identical
numbers; one run of each is kept. Run 03's failure message differed between sessions: `inflate failed` (kept in
`03_x11vnc_4.0.0`) and the length assertion (`06_abc_classpath/x11vnc_A`).

## Input (run 07, TigerVNC depth 16)

`*.stdout` lists, per step, what OculiX sent (RFB tap: keysyms, pointer button masks) and what the X server
received (`xev`). `tap_*.log` and `xev_*.log` are the raw logs.

Works: hover, mouseMove, click (mask 1), doubleClick, rightClick (mask 4), wheel (mask 16 down, 8 up), dragDrop,
drag + dropAt; typing of all printable ASCII; `"\r"`, Tab, Esc, Backspace, arrows, Home, End, Page Up, Insert, F5;
Ctrl, Ctrl+Alt, Alt+Tab; keyDown / keyUp of a modifier around a key or a click.

Does not behave as expected (both arms):

1. `mouseDown(Button.LEFT)` sends no button (it tests against `Mouse.LEFT`). `mouseDown(Mouse.LEFT)` works.
2. `mouseUp()` with no argument releases nothing. `mouseUp(Mouse.LEFT)` works, and
   `mouseUp(Mouse.LEFT | Mouse.MIDDLE | Mouse.RIGHT)` releases everything (mask 7 to 0).
3. `type(Key.ENTER)` sends Linefeed `0xff0a`; `type("\r")` sends Return `0xff0d`.
4. Ctrl+Shift+letter sends the lowercase keysym with Shift held. TigerVNC's server presses Caps Lock to
   compensate and leaves it on.
5. `Key.DELETE` sends KP_Delete `0xff9f`. `KeyModifier.META` sends Meta_L `0xffe7`.
6. `type()` with a non-ASCII character and `paste()` send only Ctrl+V; the text never arrives.

Every call returns success, including those that delivered nothing.

## Input on the Pi (run 10)

Order of events: `first_attempt/` is a false start. The Pi had been rebooted and was on its launcher, not in the
terminal as assumed, and the "before" capture was not looked at before typing. `cat -v` + Return typed on the
launcher selected the first sample tile and started `gtk4-demo`; it was closed over SSH (`slay -f gtk4-demo`).
The run proper then went: pointer steps on the launcher (`launcher.stdout`), click on the Terminal tile, login,
`cat -v`, keyboard steps (`keys.stdout`), Ctrl+D, `sudo shutdown`. The Pi rebooted to the launcher; the raw
snapshot afterwards (`after_reboot.png`) equals the one before the first key (`raw_before.png`) pixel for pixel,
and no new core dump was written.

- Pointer: 68 PointerEvents sent, the Pi's `screen_probe` event counter read 68. Hover and moves change no pixel
  of the VNC frame. `click` on the Terminal tile opens `st`. Clicks, right click, drag and held buttons on empty
  background have no visible effect. doubleClick and wheel: N/A on this image, not sent.
- Keys in `st` (tty echo and the output of `cat -v`, see `captures/keys_25.png` and `captures/ctrld_01.png`):
  all printable ASCII arrives exactly; `"\r"` ends the line; **`Key.ENTER` does nothing**; Tab works;
  Backspace arrives as `^H` and does not erase; arrows arrive as `OA OB OD OC`, Home / End as `OH OF`;
  `Key.DELETE` arrives as `.`; Ctrl+A is `^A`, Ctrl+D ends input; **Ctrl+Shift+A and Ctrl+Alt+T arrive as `^@`**;
  no Caps Lock is left on; Alt+x arrives as `x`, Meta+d as `d`; a held Shift turns `abc1` into `ABC!`;
  non-ASCII `type()` and `paste()` deliver nothing.

## Probes

| File | Purpose |
|---|---|
| `probes/ReadOnlyProbe_port.java`, `ReadOnlyProbe_debug.java` | The read-only probe as run here (port argument; optional `VNCClient.onDebugging()`). The maintained copy is `host/spikes/probe/ReadOnlyProbe.java` |
| `probes/rfb_tap.py` | Pass-through TCP proxy that logs the messages a VNC client sends (keys, pointer, pixel format, encodings) |
| `probes/InputProbe.java`, `run_input.sh` | 38 input steps against a private X display with a full-screen `xev` |
| `probes/MouseDownProbe.java`, `RecoveryProbe.java` | `Button.*` against `Mouse.*` in `mouseDown`; stuck-button recovery |
| `probes/SubRegionProbe.java`, `run_subregion.sh` | Stale-pixel test for `capture(x,y,w,h)` |
| `probes/PiInputProbe.java`, `run_phase.sh` | Input on the Pi, in phases with a capture after every step |

The input probes send pointer and key events: use them only against a display nobody else is using, or in an
agreed slot, and look at the screen before the first key.
