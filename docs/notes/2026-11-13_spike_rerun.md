# Rerun of the phase 2 spikes on the Pi 5 (2026-11-13)

David asked for the tests of 2026-11-12 ([2026-11-12_spikes.md](2026-11-12_spikes.md)) to be rerun one at a time,
watching the Pi's HDMI screen, with the target's logs monitored for problems the GUI doesn't show. 14 tests: S1 to S6
split into single checks. The run was split into visible tests (something changes on the HDMI screen), each started
only on David's go, and non-visible tests.

## Setup

- Target: Pi 5 at 10.30.1.116, unchanged since 2026-11-12 (`/tmp/screen_probe` SHA-256 `6eded5f1...` still there;
  the three cores of the 21:43 UTC crash still in `/data/var/dumper/`, no new ones). It was in the **native session**
  (`fullscreen-winmgr -s demolauncher,st,gtk` + `demolauncher`, no Weston), showing the launcher. Yesterday S2 to S6
  ran in the Developer Desktop session (Weston), mostly over its lock screen; S1 ran in the native session.
- Same programs as yesterday: `agent/spikes/screen_probe.c` (as root via `sudo`), `host/tools/vnc_snapshot.py`,
  `vnc_input.py`, `vnc_rate.py`, `host/spikes/s1/S1VncScreen.java` with OculiX 4.0.0 (pinned jar) and the same Gears
  tile pattern.
- Log monitoring: `slog2info -w` (root) streamed to the Windows host for the whole run. After every test: new log lines
  matching error/fail/fault/terminate/crash/core/lost/closed, the contents of `/data/var/dumper/`, and whether
  `drm-rpi5`, `screen`, `rpi_thermal`, `mtouch`, `vncserv`, `fullscreen-winmgr` and `demolauncher` were running.
- Captures were written to `/tmp` on the Pi and deleted after each test.

## Results: 12 pass, 2 fail

| # | Test | Yesterday | Today | |
|---|---|---|---|---|
| 1 | S4a: Screen idle timeout, idle state, power mode; metrics reset on read | 0, 0, ON; reset | 0, 0, 30339 (ON); after 3 pointer moves `update=3`, then all zeros on the next read | pass. `update_pixels` per update was about 3.7 M, not 2,073,600 (1920 x 1080) as yesterday |
| 2 | S2a: capture as `qnxuser` | `EACCES` | `EACCES`; Screen logs `wants privileged client (uid=1000 ... err=13)` | pass |
| 3 | S2b: full capture as root | 0.03 s | 0.045 s; a VNC snapshot from the host took 0.57 s including connect | pass |
| 4 | S2c: 400 x 300 region capture equals the same crop of the full capture | equal (over the lock screen) | **3,305 pixels differ**, by up to 76 levels, only on the launcher tiles' rounded borders and the terminal icon's outline; no shift; reproducible on a screen confirmed static | **fail** |
| 5 | S2d: Screen capture vs VNC frame | 68 pixels over 8 levels, all in 8 x 15 at (0,0) | 2,043,394 of 2,073,600 differ slightly; the same 68 over 8 levels, same box | pass |
| 6 | S3a: window manager context | `EBUSY` | `EBUSY`; Screen logs `Failed to set global window manager rc=16` | pass |
| 7 | S3b: display manager context | window count 0, CREATE for the display and 5 devices | the same | pass |
| 8 | S4b: keep-awake window | `KEEP_AWAKES` 1 | 0, 1 while posted, 0 after; idle 0, power ON throughout | pass (the probe's 6 s watch ran to t = 10 s, a probe quirk) |
| 9 / V1 | S5a: `screen_notify(UPDATE)` pulses vs Screen's update counter, 5 pointer moves | 60 = 60 | **44 pulses vs 5 updates** (and 5 events); three runs: 50/6, 44/5, 44/5; the pulses all within a second of the moves | **fail** |
| V2 | S5b: the same with re-arming after each pulse | 6 = 6 | 44 vs 5, identical to V1: re-arming changes nothing | pass on re-arming; same mismatch |
| 11 | S5c: static screen, 8 s | 0 | 0 pulses, update counter 0 | pass |
| 12 | S6a: `vncserv` incremental updates, raw | one full-frame rect per request, 8.29 MB, 5-6 frames/s, ~27 ms CPU a frame | the same rect behaviour; 23 frames in 4.1 s (5.6/s); ~13 ms `vncserv` CPU a frame | pass |
| 13 | S6b: the same with ZRLE | 1.22 MB a frame, 2.2 frames/s, ~0.4 s CPU a frame | 0.51 MB a frame, 22 in 4.1 s (5.3/s), ~0.15 s CPU a frame | pass; size and speed depend on screen content (the launcher compresses better than the lock-screen photo) |
| 14 / V3 | S1: OculiX connect, capture, `exists`, workaround `Finder`, click | ClassCastException; found at 1.0 at (958,768); click launched gears | start 3.29 s, capture 0.21 s; the same ClassCastException; found at 1.0000 at (958,768) in 0.10-0.24 s; click launched `gles3-gears` (process checked, capture confirms) | pass |

V1 to V3 are the visible tests (the pointer moves, gears opening), run one at a time on David's go. David filmed them.
Tests 1 to 9 ran before the split; test 1, 7 and 9 also moved the pointer.

## What the failures mean

- **Test 4:** a region capture is not guaranteed to be byte-identical to the same crop of a full capture where the
  screen has blended edges. Text and flat areas matched exactly. Cause not known (a different readback path for a
  region is a guess). For the agent: compare captures with a tolerance, which the VNC cross-check needs anyway
  (test 5).
- **Test 9:** in the native session the update pulse fires several times per actual change (about 9 per pointer move
  here), while under Weston it matched Screen's counter one to one. It still separates "something changed" from
  "nothing changed" (test 11: 0 on a static screen), which is what frame freshness needs, but it must not be used as a
  frame count. Cause not known (pointer layer updates are a guess).

Neither changes the design proposals of 2026-11-12 (freshness via `screen_notify` only; capture cross-check with a
tolerance).

## Seen only in the logs or the status, not on the GUI

- Screen's own log lines for the refusals of tests 2 and 6.
- No core dumps, no process exits, and no graphics errors during the whole run.
- The wired interface's IPv6 default router (`fe80::5aef:68ff:fe61:4222` on `cgem0`) is dropped and re-added by
  `dhcpcd` about every 76 s. Harmless for these tests; not followed up.
- Boot-time log lines, for reference: `mtouch` `touch_display[ERROR]: couldn't find display hdmi` and a HID
  X/Y-count mismatch; Wi-Fi DMA allocation failures in `io_sock` ("Disabling DMA support").

## The screen went black

Before the run, the HDMI screen was black and David moved the mouse to wake it. At the same time Screen reported the
display idle timeout 0 (off), idle state 0 and power mode ON, and the log had no blanking, power or HDMI message. So
**the native session does blank**, contrary to yesterday's guess, and neither Screen's state nor the log shows it.
How long it took and what blanks it (the monitor's own power saving is a guess) are open. A test run that meets a
black screen fails for the wrong reason, so this is part of the blanking TODO in [../PLAN.md](../PLAN.md).
