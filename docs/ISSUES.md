# Issues found by our testing

Every defect our tests found in OculiX, in QNX and in third-party VNC servers, with an internal ID for tracking.
Started 2026-11-14. The evidence and measurements are in [OCULIX_VNC_MATRIX.md](OCULIX_VNC_MATRIX.md).

How issues are reported:

- **OculiX** (open source): the Oculix session drafts each report in David's fork. David reviews it and raises it
  upstream together with its fix, as a pull request. Nothing is filed until David has reviewed it.
- **QNX**: defects are reported to QNX **privately**, through QNX support, with the evidence attached. Only the
  internal ID, a one-line title and the QNX reference are listed here, so that QNX support and the QNX development
  team can find each report.
- **Third-party VNC servers**: recorded here; reported upstream only if we show the cause.

## QNX

| ID | Issue | Status | QNX reference |
|---|---|---|---|
| ON-QNX-001 | `vncserv` 4.2.0 (quick-start image 20260902) crashed once after a client disconnected ([note](notes/2026-11-14_vncserv_crash.md)) | case write-up ready; to be reported to QNX support privately; not reproduced in 280 further connections (140 on 4.2.0, 140 on 4.2.1); the Pi now runs 4.2.1 | to be added |
| ON-QNX-002 | Graphics stack on the Pi 5: `drm-rpi5`, `screen` and `rpi_thermal` terminated together (2026-11-12, [spikes note](notes/2026-11-12_spikes.md)), and `drm-rpi5` again on 2026-11-13 | under investigation; not reported | none yet |

## OculiX 4.0.0 (with its VNC client, tigervnc-java-oculix)

"Draft" is the Oculix session's report number where one exists. Where a fix exists in a newer VNC client jar, the
ON-VNC-1 configuration works around it (docs/HOW_WE_WORK.md).

| ID | Issue | Workaround in use | Draft |
|---|---|---|---|
| ON-OX-001 | The ZRLE decoder of the bundled VNC client (2.0.1) corrupts or blanks frames at 32 bpp (TigerVNC, x11vnc, TightVNC, macOS) ([draft](upstream/oculix-zrle-decoder-tigervnc.md)) | ON-VNC-1 (2.0.2 or 2.0.4 first) | 9 and 10 (comments on #481 and #443) |
| ON-OX-002 | rgb565 frames are decoded with wrong green and blue ([draft](upstream/oculix-rgb565-colours.md)) | ON-VNC-1 (2.0.4 asks for 32 bpp) | 2 |
| ON-OX-003 | `find`, `findAll`, `text()` on a `VNCScreen` throw `ClassCastException` for a 16 bpp server ([draft](upstream/oculix-vncscreen-find-classcast.md)) | ON-VNC-1; or `Finder` on a BGR copy | 1 |
| ON-OX-004 | tigervnc-java 2.0.4 loops when a server sends the security-type count and list in separate writes (TightVNC 2.8.90) | ON-VNC-1 uses 2.0.2 for TightVNC | 7 |
| ON-OX-005 | tigervnc-java 2.0.4 picks Apple DH (type 30) on macOS Screen Sharing, so a VNC-password login fails | ON-VNC-1 limits the security types to `VncAuth,None` | 8 |
| ON-OX-006 | `Key.ENTER` sends Linefeed (0xff0a), not Return; apps and QNX ignore it | `type("\r")` | fixed in the fork, unreleased; see draft 4 |
| ON-OX-007 | `Key.DELETE` sends keypad Delete (0xff9f) from a Linux client; QNX shows "." | ON agent sends Delete itself | 4 |
| ON-OX-008 | From a Windows client, Windows key codes are read as Java key codes: `Key.DELETE` ".", `Key.INSERT` "-", `Key.PRINTSCREEN` ",", `Key.META` "["; `Key.F13` to `F15` and `Key.ALTGR` throw; `write("#ENTER.")` throws | `type("\r")`; ON agent sends these keys itself | 11 |
| ON-OX-009 | `Button.*` constants do nothing in `mouseDown`/`mouseUp`; a bare `mouseUp()` re-sends the press and leaves the button held | `Mouse.*` constants only | 3 |
| ON-OX-010 | `paste()` and `type()` with non-ASCII text send only Ctrl+V and no clipboard message; nothing arrives, and the call returns success | type the text; ON agent | 5 |
| ON-OX-011 | `type("a", SHIFT)` and Ctrl+Shift+letter send the lowercase keysym with Shift held (x11vnc gives "a"; TigerVNC leaves Caps Lock on) | type capitals as capitals; avoid Ctrl+Shift | 6 (a comment on #334) |
| ON-OX-012 | `KeyModifier.META` sends Meta_L, not Super | none needed so far | 12 |
| ON-OX-013 | Partial captures and `getColor` send update requests with width and height where the code expects corners (`VNCClient.java:167`); servers clip them, so results are right | none needed | 13 (low severity; measured on the wire by ON-UBUNTU24; results right) |
| ON-OX-014 | OCR does not load on macOS: OculiX 4.0.0 pins Legerix 5.5.0-8 (Legerix#20, fixed in 5.5.0-9); on Linux it needs `LD_LIBRARY_PATH` | OCR from Windows or Linux | none (upstream: Legerix#20 for macOS; Legerix#26 for Linux, its RUNPATH is a CI path plus a broken `$ORIGIN`) |
| ON-OX-015 | Calls return success when nothing was delivered, and a decoder exception kills the reader thread while the script carries on | check stderr for `Thread-N` exceptions; keep a capture | tracker only; covered by drafts 3, 4, 5 and 9 |

## Third-party VNC servers

| ID | Issue | Status |
|---|---|---|
| ON-EXT-001 | TightVNC 2.8.90 (Windows): shifted punctuation intermittently arrives unshifted (7 of 21, then 2 of 7), although the client sent the right keysyms with Shift | recorded; cause not shown |
