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

- Does QNX 8.0 already include a VNC server or a Screen remote-display component? Partly answered 2026-11-12: it
  ships `target.screen.vncserv` (VNC server for QNX Screen), installed on both hosts; not yet evaluated.
- Which Screen API calls cover display capture and input injection, and do they need root or special permissions?
- How does the Pi 5 quick-start image handle Screen and input (USB keyboard and mouse, touch)?

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
