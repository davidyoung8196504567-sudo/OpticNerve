# OpticNerve plan

Written 2026-11-12. Hackathon submission deadline: **2026-12-15, 14:00 Eastern**.

## Goal for the submission

A Raspberry Pi 5 running QNX 8.0 shows a demo HMI on HDMI. On a laptop, an Oculix script connects to the OpticNerve
agent, sees the screen, taps buttons, reads text and reports pass or fail. The same works against a QEMU target. The
demo video (under 3 minutes) shows this end to end.

Category: Developer Tooling & Host Tools (phase 1 decided against porting a VNC library, so Open-Source Contribution
& Ports does not apply). Bonus entries: Best Use of QNX-specific Technologies, Best Codelab/Tutorial.

## Phases

| # | Phase | Done when | Target date |
|---|---|---|---|
| 0 | Setup | Linux build host on SDP 8.0.5; Pi 5 boots QNX from a spare microSD; QEMU target boots | 2026-11-19 |
| 1 | Survey and design | Know whether QNX 8.0 ships a VNC or remote-screen component and what Screen offers for capture and input injection; choose: port a VNC library or write the server | 2026-11-21 |
| 2 | Spikes and agent core | Spikes S1 to S6 answered ([DESIGN.md](DESIGN.md)); agent runs as a resource manager (`/dev/opticnerve`) with frame freshness, idle/power state and keep-awake | 2026-11-28 |
| 3 | Capture and host access | Region capture (`screen_read_display()`); the `on` command on the target; host helpers over SSH | 2026-12-03 |
| 4 | Window list and hardening | Window list if spike S3 allows; error handling, reproducible deployment of the agent | 2026-12-07 |
| 5 | Oculix suite | Demo HMI (a native Screen application) on QNX; Oculix test suite and preset on the host; runs green on Pi 5 and QEMU | 2026-12-10 |
| 6 | Submission | Build and run instructions, description naming the QNX-specific files, video, tutorial | 2026-12-14 |

## Rules to keep (from the official rules, read 2026-11-12)

- Public repo with an open-source licence; all source and build instructions; it must really build for or run on QNX
  8.0. List the QNX version, target and toolchain.
- Original work, solely owned by the entrant. No proprietary third-party code or assets. The free QNX Everywhere
  licence only.
- Video under 3 minutes on YouTube, showing it running on the device; no third-party trademarks or music without
  permission.

## Open questions

- Does QNX 8.0 already include a VNC server or a Screen remote-display component? Answered 2026-11-12: yes,
  `vncserv` (package `target.screen.vncserv`). On the Pi 5 quick-start image it runs by default on port 5900; it
  serves the whole display (GLES2, Vulkan, video layers, the Weston/XFCE Developer Desktop) and forwards keys,
  pointer moves and clicks. Details: [notes/2026-11-12_pi5_first_boot.md](notes/2026-11-12_pi5_first_boot.md).
  Decided 2026-11-12: build the ON agent on `vncserv`, not a VNC server of our own ([DESIGN.md](DESIGN.md)).
- Which Screen API calls cover display capture and input injection, and do they need root or special permissions?
  Answered from the Screen docs in [DESIGN.md](DESIGN.md) (`screen_read_display()` needs root and a display
  manager context); to be confirmed on the Pi by spikes S2 and S3.
- How does the Pi 5 quick-start image handle Screen and input? Answered: Screen on HDMI0 at 1920 x 1080, input via
  `vncserv` works. Open: `-use_touch`, a second HDMI display, and one black frame seen after the desktop started.
- TODO: screen blanking and the lock screen on the Developer Desktop. On the Pi 5, power management is on by
  default: the screen blanks when idle, and moving the mouse wakes it into a locked state that needs a click on a
  green dot to unlock (David, 2026-11-12; seen before on another board running Wayland). A test run that meets a
  blank or locked screen fails for the wrong reason. Leads: `~/.config/weston.ini` on the Pi has `locking=true`
  (twice) plus `lockscreen=` and `lockscreen-icon=` in `[shell]`; Weston's idle timeout (`idle-time` in `[core]`) is
  the likely trigger (to verify). The one black frame captured after the desktop started may have been a blanked
  screen. Decide: turn blanking/locking off on test targets (and record the change like any target change), or have
  the test tooling detect a blank or locked screen, wake and unlock it, and report it. Check whether the native
  Screen session (without Weston) blanks too.
- QEMU target: QNX supports Ubuntu 22.04/24.04 hosts only. Under WSL it ran but too slowly to be useful
  (2026-11-12); it moves to a dedicated Ubuntu 24.04 host. See
  [notes/2026-11-12_qemu_guide.md](notes/2026-11-12_qemu_guide.md).

## Status on 2026-11-12 (day 1)

- Phase 0: build hosts done (Windows and Linux at SDP-BL-005, not "SDP 8.0.5": see the config management doc); Pi 5
  boots QNX from its own microSD (the Raspberry Pi OS card is separate). QEMU target not yet booted.
- Phase 1: done. Design [DESIGN.md](DESIGN.md) accepted by David: build on `vncserv`, the ON agent adds frame
  freshness, idle/keep-awake, region capture and status via `/dev/opticnerve`; the demo HMI is a native Screen
  application; phases 2 to 4 above were rewritten to match.
- Phase 2: spikes S2 to S6 done on the Pi ([notes/2026-11-12_spikes.md](notes/2026-11-12_spikes.md)), with proposed
  design changes (drop keep-awake and the window list; freshness via `screen_notify` only). S1 waits for Oculix.

## OculiX issues found: fix after the ON agent work

David contributes to OculiX through his fork <https://github.com/davidyoung8196504567-sudo/Oculix>. Problems we find
in OculiX are recorded under [upstream/](upstream/) and fixed in the fork once the ON agent work is done; until then
we use workarounds and stay on the pinned OculiX 4.0.0 jars.

- [ ] `VNCScreen` image search throws `ClassCastException` in `Commons.getMatList`
      ([upstream/oculix-vncscreen-find-classcast.md](upstream/oculix-vncscreen-find-classcast.md)). Workaround: search
      a `TYPE_3BYTE_BGR` copy of the capture with `Finder`.

## Hackathon project page: to do

Registered 2026-11-12 with the "About the project" text in [submission/about-the-project.md](submission/about-the-project.md),
the brand banner, the "Built with" tags and the GitHub repo as the "Try it out" link. Project page:
<https://devpost.com/software/opticnerve> (a draft: on 2026-11-12 it redirected visitors to a login page).

- [ ] Link the project page from the README once it is public (after submission). Until then the link would send
      visitors to a login page.

- [ ] Add the YouTube demo video link in the page's own "Demo video URL" field under "Project Media". Due with
      phase 6, by 2026-12-14. YouTube only, public or unlisted, no password. Only the first 3 minutes are reviewed, so
      everything needed to judge the project must be in them.
- [ ] Add images to the "Project Media" gallery as they exist (JPG, PNG or GIF, 5 MB max, 3:2, up to 15): the Pi 5
      screen in a stock VNC viewer, an Oculix test run going green, the Pi 5 and QEMU targets side by side. The
      brand banner went in at registration.
- [ ] Fill in "Link to your codelab" once the phase 6 tutorial is published (needed to compete for the Best
      Codelab/Tutorial bonus prize). Left empty at registration.
- [ ] Update "About the project" before the deadline: it was written on day one and says the agent doesn't exist yet.
