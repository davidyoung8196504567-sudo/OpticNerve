# Issues found by our testing

Every defect our tests found in OculiX and in third-party VNC servers, with an internal ID for tracking.
Started 2026-11-14. Three entries have full write-ups in [upstream/](upstream/).

How issues are reported:

- **OculiX** (open source): the Oculix session drafts each report in David's fork. David reviews it and raises it
  upstream together with its fix, as a pull request. Nothing is filed until David has reviewed it.
- **Third-party VNC servers**: recorded here; reported upstream only if we show the cause.

## OculiX 4.0.0 (with its VNC client, tigervnc-java-oculix)

"Who can fix it" (Oculix session, 2026-11-15): if OculiX puts the wrong bytes on the wire, no server can repair them;
those are OculiX-only. Server-side entries are handled by our own VNC server for QNX, which corrects only
what a server should do and does not compensate for OculiX bugs.

"Draft" is the Oculix session's report number where one exists. Where a fix exists in a newer VNC client jar, the
ON-VNC-1 configuration works around it (defined in [CONFIG_MANAGEMENT.md](CONFIG_MANAGEMENT.md), row "VNC client
library override").

| ID | Issue | Workaround in use | Draft | Who can fix it |
|---|---|---|---|---|
| ON-OX-001 | The ZRLE decoder of the bundled VNC client (2.0.1) corrupts or blanks frames at 32 bpp (TigerVNC, x11vnc, TightVNC, macOS) ([draft](upstream/oculix-zrle-decoder-tigervnc.md)) | ON-VNC-1 (2.0.2 or 2.0.4 first) | 9 and 10 (comments on #481 and #443) | client-side: OculiX only (our server can avoid triggering it) |
| ON-OX-002 | rgb565 frames are decoded with wrong green and blue ([draft](upstream/oculix-rgb565-colours.md)) | ON-VNC-1 (2.0.4 asks for 32 bpp) | 2 | server-side: OpticNerve server |
| ON-OX-003 | `find`, `findAll`, `text()` on a `VNCScreen` throw `ClassCastException` for a 16 bpp server ([draft](upstream/oculix-vncscreen-find-classcast.md)) | ON-VNC-1; or `Finder` on a BGR copy | 1 | server-side: OpticNerve server |
| ON-OX-004 | tigervnc-java 2.0.4 loops when a server sends the security-type count and list in separate writes (TightVNC 2.8.90) | ON-VNC-1 uses 2.0.2 for TightVNC | 7 | server-side: OpticNerve server |
| ON-OX-005 | tigervnc-java 2.0.4 picks Apple DH (type 30) on macOS Screen Sharing, so a VNC-password login fails | ON-VNC-1 limits the security types to `VncAuth,None` | 8 | server-side: OpticNerve server |
| ON-OX-006 | `Key.ENTER` sends Linefeed (0xff0a), not Return; applications ignore it (measured on TigerVNC and x11vnc) | `type("\r")` | fixed in the fork, unreleased; see draft 4 | client-side: OculiX only (fixed upstream, unreleased) |
| ON-OX-007 | `Key.DELETE` sends keypad Delete (0xff9f) from a Linux client, not Delete (0xffff) | ON agent sends Delete itself | 4 | server-side: OpticNerve server |
| ON-OX-008 | From a Windows client, Windows key codes are read as Java key codes: `Key.DELETE` ".", `Key.INSERT` "-", `Key.PRINTSCREEN` ",", `Key.META` "["; `Key.F13` to `F15` and `Key.ALTGR` throw; `write("#ENTER.")` throws | `type("\r")`; ON agent sends these keys itself | 11 | client-side: OculiX only |
| ON-OX-009 | `Button.*` constants do nothing in `mouseDown`/`mouseUp`; a bare `mouseUp()` re-sends the press and leaves the button held | `Mouse.*` constants only | 3 | client-side: OculiX only |
| ON-OX-010 | `paste()` and `type()` with non-ASCII text send only Ctrl+V and no clipboard message; nothing arrives, and the call returns success | type the text; ON agent | 5 | client-side: OculiX only |
| ON-OX-011 | `type("a", SHIFT)` and Ctrl+Shift+letter send the lowercase keysym with Shift held (x11vnc gives "a"; TigerVNC leaves Caps Lock on) | type capitals as capitals; avoid Ctrl+Shift | 6 (a comment on #334) | server-side: OpticNerve server |
| ON-OX-012 | `KeyModifier.META` sends Meta_L, not Super | none needed so far | 12 | server-side: OpticNerve server |
| ON-OX-013 | Partial captures and `getColor` send update requests with width and height where the code expects corners (`VNCClient.java:167`); servers clip them, so results are right | none needed | 13 (low severity; measured on the wire by ON-UBUNTU24; results right) | client-side: OculiX only (no action needed) |
| ON-OX-014 | OCR does not load on macOS: OculiX 4.0.0 pins Legerix 5.5.0-8 (Legerix#20, fixed in 5.5.0-9); on Linux it needs `LD_LIBRARY_PATH` | OCR from Windows or Linux | none (upstream: Legerix#20 for macOS; Legerix#26 for Linux, its RUNPATH is a CI path plus a broken `$ORIGIN`) | client-side: OculiX only (upstream) |
| ON-OX-015 | Calls return success when nothing was delivered, and a decoder exception kills the reader thread while the script carries on | check stderr for `Thread-N` exceptions; keep a capture | tracker only; covered by drafts 3, 4, 5 and 9 | client-side: OculiX only |
| ON-OX-016 | The viewer in tigervnc-java-oculix 2.0.4 (`com.tigervnc.vncviewer.VncViewer`) does not start: the jar holds no resources besides its manifest and Maven files, so the viewer's icon is missing and it fails with `NullPointerException` in `ImageIcon.<init>` (`VncViewer.java:74`, class initialiser). 2.0.2 has no images either. Found 2026-11-19 on Windows, Temurin 25.0.4+7; reproduced on macOS by the Oculix session. Already reported upstream as [tigervnc-java-oculix#12](https://github.com/oculix-org/tigervnc-java-oculix/issues/12) (2026-07-26, against 2.0.3): the images sit in `src/main/java`, which the build does not copy | the TigerVNC 1.16.2 viewer for Windows (`vncviewer64-1.16.2.exe`) | none (upstream issue #12 is open) | client-side: OculiX only (packaging; only matters if the jar is meant to be usable as a viewer) |

## OpticNerve agent (ours)

| ID | Issue | Workaround in use | Status |
|---|---|---|---|
| ON-AG-001 | The agent sends no cursor: it offers only Raw encoding, without the Cursor pseudo-encoding (-239), and the pointer is not part of the frames it reads from the screen, so a VNC viewer shows no pointer (found 2026-11-19 with TigerVNC 1.16.2) | none needed now; TigerVNC's `AlwaysCursor=1 CursorType=System` keeps the viewer's own pointer visible as well | done, phase 1 (2026-11-23): a viewer that asks for a pointer shape (Cursor, XCursor or CursorWithAlpha) is sent an arrow drawn into its frames at the last position the agent injected; test tools, which ask for none, get frames without it. On by default; `opticnerve pointer on\|off` on the target, or `ON_AGENT_POINTER=0` for `opticnerve_deploy.py start`. Phase 2 (done 2026-11-24): while such a viewer is connected the agent holds the target's own USB mouse and injects its moves, so the arrow follows it too; `opticnerve mouse on\|off`, or `ON_AGENT_MOUSE=0` |
| ON-AG-002 | The demo panel (`agent/demo/demo_panel.c`) has no way to close it from its own screen: a person at the Pi or in a viewer cannot leave it without SSH (asked by David, 2026-11-23) | `sh host/poc/demo-panel.sh stop` (over SSH) | done (2026-11-24): an unlabelled X in the top right corner of the panel's title bar, as in any windowed GUI (away from +1, RESET and LAMP, so no test clicks it by accident); red while pressed, and only a release inside it closes the panel. Watched test `test_demo_panel_close_x` |
| ON-AG-003 | Opening the quick-start image's apps from a viewer or a test, as often as a test run needs, took SSH (asked by David, 2026-11-24) | `ssh` to the target and start the app's command as the image's launcher configuration gives it | done (2026-11-24): the launcher panel (`agent/demo/launch_panel.c`, `host/tools/launch_panel.sh`): a tile for each app of the launcher configuration and one for GTK4 Widget Factory; a click starts the app, from a viewer or a test, as often as needed; Alt+Tab+Del with Tab held closes it (from a script or the target's keyboard: see ON-AG-004). Tests `test_launch_panel_reads_the_configuration`, `test_launch_panel` (watched) |
| ON-AG-004 | A person in a viewer on a laptop cannot send Alt+Tab or Alt+Tab+Del to the target: the laptop keeps Alt+Tab, so an app without a close control of its own cannot be closed or switched away from in the viewer (seen by David with the OculiX viewer, 2026-11-24) | the target's own keyboard; or a script that sends the keys through the agent | done (2026-11-25): the agent's key chord: hold Q and tap N for Alt+Tab, hold Q and N and press X for Alt+Tab+Del with Tab held; typing is unchanged (`agent/core/on_core.c`, `-nochord` to turn it off). Tests: `core_test` (the chord checks), `test_key_chord` (watched) |

## Third-party VNC servers

| ID | Issue | Status |
|---|---|---|
| ON-EXT-001 | TightVNC 2.8.90 (Windows): shifted punctuation intermittently arrives unshifted (7 of 21, then 2 of 7), although the client sent the right keysyms with Shift | recorded; cause not shown |
