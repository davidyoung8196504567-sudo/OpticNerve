# OculiX 4.0.0 over VNC, measured from UBUNTU24 (2026-11-13 / 14 UTC)

Raw results, logs, captures and probe sources of the ON-UBUNTU24 session. The conclusions are in
[docs/OCULIX_VNC_MATRIX.md](../../../docs/OCULIX_VNC_MATRIX.md) and [docs/upstream/](../../../docs/upstream/), kept
by the ON-ASUSROG-win-11 session; this folder is the evidence behind the Ubuntu rows. Shared at David's request.
No passwords are stored here: programs take them as arguments or from the environment, and the keysyms of the Pi
login password are removed from the tap logs of runs 10 and 12 (in run 12 also OculiX's own log line of the typed
text).

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
| TightVNC 2.8.90, Windows 11 host (ASUS ROG) | 10.30.1.114:5900 | 32 bpp depth 24, 2560x2520 (two screens stacked); offers VncAuth(2) and Tight(16) |

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
| `runs/11_tightvnc_windows` | Read-only probe, arms A / B / C, TightVNC on the Windows host. Logs only | A: connects, `ZlibInStream: inflate failed` at `ZRLEDecoder.java:68`, frame all black. B: exact capture, find and OCR work. C: loops in the security handshake in 3 of 4 runs, connects in 1. See "TightVNC" below |
| `runs/12_pi_input_gui` | Second input slot on the Pi, ON-VNC-1: mouse press / drag / release and modifier combinations in `gtk4-demo`, Backspace in `st` | See "Input on the Pi, second slot" below |

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
  Backspace arrives as `^H` and does not erase in `cat -v` (it does at the shell prompt, see run 12);
  arrows arrive as `OA OB OD OC`, Home / End as `OH OF`;
  `Key.DELETE` arrives as `.`; Ctrl+A is `^A`, Ctrl+D ends input; **Ctrl+Shift+A and Ctrl+Alt+T arrive as `^@`**;
  no Caps Lock is left on; Alt+x arrives as `x`, Meta+d as `d`; a held Shift turns `abc1` into `ABC!`;
  non-ASCII `type()` and `paste()` deliver nothing.

## TightVNC (run 11)

Read-only; no input was sent. The captures and OCR text of this run are not stored here, because they show the
Windows host's working desktop; only the logs are. The raw reference is the second frame of
`probes/vnc_snapshot_two_frames.py`: the first frame TightVNC sends to `host/tools/vnc_snapshot.py` is all black
(6,451,200 of 6,451,200 pixels).

- Arm A (`A.stdout`, `A.stderr`): VncAuth chosen, then the reader thread dies with `ZlibInStream: inflate failed`
  (`ZRLEDecoder.readRect(ZRLEDecoder.java:68)`). `capture()` returns an all-black type 1 image, 5,751,466 pixels
  wrong against the raw frame.
- Arm B (`B.stdout`, `B.stderr`): no exception. The capture differs from the raw frame taken before it in 0
  pixels and from the one after in 121, the same 121 in which the two raw frames differ (a live desktop).
  OCR reads 1387 characters in about 11 s for the full frame.
- Arm C, 2.0.4 first, four runs:
  - debug log on, `secTypes` limited to `VncAuth,None` (`C_debug_secTypes.stderr.head`): "Processing security
    types message" repeated 38,434,239 times in 240 s, a 1.8 GB log, until the timeout killed it.
  - debug log on, no limit on `secTypes` (`C_debug.stdout`, `C_debug.stderr.head`): the same loop, killed at 120 s.
  - no debug log, `secTypes` limited: no capture and no output in 45 s, killed.
  - no debug log, no limit: **connected**. A capture was written 8 s after the start; it differs from the raw
    frame taken 23 s before it in 130 pixels and from the one taken three minutes before in 109, all inside
    the area where the raw frames differ from each other. The probe was still running its OCR steps when the 45 s limit killed it, and its stdout
    was lost, so there is no log of this run. It was first reported to the fleet as "no connection", which was
    wrong.

  The cause was found by the Oculix session: `processSecurityTypesMsg` in 2.0.4 misreads the list of security
  types when the count and the list arrive in separate TCP segments. That fits a server that connects in some
  runs and not in others. ON-VNC-1 uses the 2.0.2 jar for TightVNC.

## Input on the Pi, second slot (run 12)

`*.steps` are the scripts for `probes/StepProbe.java`, `*.stdout` what OculiX sent per step, `captures/` the
screen after every step, in this order: `open` (the launcher's GTK4 demo tile), `g1a`, `g1b` (mouse), `g2a`,
`g2b`, `g2c` (modifiers), then `gtk4-demo` closed over SSH (`slay -f gtk4-demo`), `t1` to `t6` (Terminal tile,
login, shell, `sudo shutdown`). The screen was looked at before the first key. The raw snapshot at the end
(`raw_end.png`) is byte-identical to the one before the first step (`raw_first.png`); no new core dump.

- Mouse, with text selection in the demo's Info page as the visible effect:
  `mouseDown(Button.LEFT)` and a move select nothing (`g1a`). `mouseDown(Mouse.LEFT)` and a move select text
  along the drag (`g1b_04.png`). After a bare `mouseUp()` the button is still held on the Pi: the next move
  extends the selection (`g1b_06.png`) and the next `click` sends only the release, so it ends the drag instead
  of clicking. After `mouseUp(Mouse.LEFT | Mouse.MIDDLE | Mouse.RIGHT)` a move selects nothing and a click is a
  click.
- Modifiers in GTK: Ctrl+F opens the search bar and Ctrl+A selects all text in an entry. **Ctrl+Shift+I opens
  the GTK Inspector prompt** (`g2b_06.png`, `g2b_07.png`), so Ctrl+Shift+letter reaches a GUI application as a
  shortcut; the `^@` of run 10 is specific to the terminal. Meta+d types `d`. Alt+x and Ctrl+Alt+t have no
  visible effect, but nothing is bound to them, so Alt as a modifier is not proven either way. Ctrl+Shift+A:
  inconclusive.
- After the Inspector prompt was cancelled and another widget clicked, the Pi's screen went black (`g2c`,
  `raw_g2c.png`) while `gtk4-demo` kept running. Closing it over SSH brought the launcher back unchanged
  (`raw_after_slay.png`). Avoid the Inspector on this image.
- Backspace: erases at the login prompt (`t2`, `t3`) and at the shell prompt (`t5`: `echo abcdxx`, two
  Backspaces, `EF` prints `abcdEF`). `stty -a` shows erase = `^?`; OculiX's Backspace arrives as `^H`. The login
  program and the shell accept both; a program reading the terminal directly, such as the `cat -v` of run 10,
  does not.
- `t6`: `sudo shutdown` reboots the Pi, the connection drops, and the probe ends with `EndOfStream` / `Broken
  pipe`. That is expected.
- Not measured: double click, wheel, dragging a slider or a list.

## Probes

| File | Purpose |
|---|---|
| `probes/ReadOnlyProbe_port.java`, `ReadOnlyProbe_debug.java` | The read-only probe as run here (port argument; optional `VNCClient.onDebugging()`). The maintained copy is `host/spikes/probe/ReadOnlyProbe.java` |
| `probes/rfb_tap.py` | Pass-through TCP proxy that logs the messages a VNC client sends (keys, pointer, pixel format, encodings) |
| `probes/InputProbe.java`, `run_input.sh` | 38 input steps against a private X display with a full-screen `xev` |
| `probes/MouseDownProbe.java`, `RecoveryProbe.java` | `Button.*` against `Mouse.*` in `mouseDown`; stuck-button recovery |
| `probes/SubRegionProbe.java`, `run_subregion.sh` | Stale-pixel test for `capture(x,y,w,h)` |
| `probes/PiInputProbe.java`, `run_phase.sh` | Input on the Pi, in phases with a capture after every step |
| `probes/StepProbe.java`, `run_steps.sh` | Input from a script file, one step per line, with a capture after every step (run 12) |
| `probes/vnc_snapshot_two_frames.py` | `host/tools/vnc_snapshot.py` with a port argument and a second frame, for TightVNC |

The input probes send pointer and key events: use them only against a display nobody else is using, or in an
agreed slot, and look at the screen before the first key.
