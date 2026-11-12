# OpticNerve plan

Written 2026-11-12. Hackathon submission deadline: **2026-12-15, 14:00 Eastern**.

## Goal for the submission

A Raspberry Pi 5 running QNX 8.0 shows a demo HMI on HDMI. On a laptop, an Oculix script connects to the OpticNerve
agent, sees the screen, taps buttons, reads text and reports pass or fail. The same works against a QEMU target. The
demo video (under 3 minutes) shows this end to end.

Category: Developer Tooling & Host Tools, or Open-Source Contribution & Ports if the VNC side is a port of an existing
library (decided in phase 1). Bonus entries: Best Use of QNX-specific Technologies, Best Codelab/Tutorial.

## Phases

| # | Phase | Done when | Target date |
|---|---|---|---|
| 0 | Setup | Linux build host on SDP 8.0.5; Pi 5 boots QNX from a spare microSD; QEMU target boots | 2026-11-19 |
| 1 | Survey and design | Know whether QNX 8.0 ships a VNC or remote-screen component and what Screen offers for capture and input injection; choose: port a VNC library or write the server | 2026-11-21 |
| 2 | Capture | Agent reads the display (Screen) and serves frames over VNC; a stock VNC viewer shows the Pi's screen | 2026-11-28 |
| 3 | Input | Taps, mouse and keys from VNC reach the app under test | 2026-12-03 |
| 4 | Resource manager | Agent runs as a resource manager (`/dev/opticnerve`): status, start and stop, region capture through message passing | 2026-12-07 |
| 5 | Oculix suite | Demo HMI on QNX; Oculix test suite and preset on the host; runs green on Pi 5 and QEMU | 2026-12-10 |
| 6 | Submission | Build and run instructions, description naming the QNX-specific files, video, tutorial | 2026-12-14 |

## Rules to keep (from the official rules, read 2026-11-12)

- Public repo with an open-source licence; all source and build instructions; it must really build for or run on QNX
  8.0. List the QNX version, target and toolchain.
- Original work, solely owned by the entrant. No proprietary third-party code or assets. The free QNX Everywhere
  licence only.
- Video under 3 minutes on YouTube, showing it running on the device; no third-party trademarks or music without
  permission.

## Open questions

- Does QNX 8.0 already include a VNC server or a Screen remote-display component?
- Which Screen API calls cover display capture and input injection, and do they need root or special permissions?
- How does the Pi 5 quick-start image handle Screen and input (USB keyboard and mouse, touch)?
