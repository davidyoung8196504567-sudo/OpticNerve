# Context

Background for anyone (person or AI session) picking up OpticNerve. Collected 2026-11-12; check dates before relying
on version numbers. The plan is in [PLAN.md](PLAN.md).

## What OpticNerve is

**ON** (the OpticNerve agent) is a native C program on QNX 8.0 that captures the display through QNX Screen, injects
touch, mouse and keyboard input, and serves both over VNC (RFB). **Oculix 4.x** on a Windows, macOS or Linux host
connects to it and runs image-matching GUI tests against any QNX app. Entry for the QNX Global Hackathon.

## Why no Java on the target

Oculix is Java (an MIT-licensed SikuliX1 fork) and depends on `java.desktop` (`java.awt.Robot`, `BufferedImage`,
Swing), OpenCV and Tesseract. As of 2026-11-12 there is no usable JVM for QNX 8.0:

- **OpenJDK 21 HotSpot on QNX 8.0 aarch64** exists only as a 2025 FAU master's thesis (Loganathan; CC BY 4.0). It is
  built `--enable-headless-only` with `java.desktop` excluded, was validated on QEMU only (real hardware is future
  work), and no source repository is published in the thesis or its announcement.
  <https://oss.cs.fau.de/wp-content/uploads/2026/03/Loganathan_2025.pdf>
- **aicas JamaicaVM** lists QNX 8.0 (x86_64, aarch64) but is commercial, which does not fit an open-source entry.

So Oculix stays on the host and ON does the target-side work that `java.awt.Robot` would have done.

## Hackathon rules that shape the work

Source: <https://qnx.devpost.com/rules> (read 2026-11-12). Submission deadline **2026-12-15, 14:00 Eastern**.

- Must build for or run on QNX 8.0 (real hardware or QEMU), or be a host tool supporting QNX 8.0.
- Public repo with an open-source licence shown on the repo page; all source and build/run instructions naming the
  QNX version, target and toolchain. It must really build for QNX, not just name it.
- New work in the submission period (from 2026-11-02). Original, solely owned; open-source pieces allowed if their
  licences are followed and the entry builds on them.
- Text description naming the QNX-specific files and APIs; demo video under 3 minutes on YouTube, showing it on the
  device, with no third-party trademarks or music.
- Judging, equal weights: Technological Implementation, Design, Potential Impact, Innovation & AI Creativity.
- Bonus prizes: Best Use of QNX-specific Technologies (message passing, resource managers, microkernel, boot
  customisation, real-time services, security policies) and Best Codelab/Tutorial.
- Use the free QNX Everywhere licence only: projects with a commercial licence from the sponsor are excluded.
- Keep "QNX" out of the project's own name (trademark); "for QNX 8.0" in descriptions is fine.

## Hosts

QNX SDP 8.0 has **no macOS or ARM host** support. Supported (8.0.3 / 8.0.4 release notes): Windows 10/11, Ubuntu
Desktop 24.04 and 22.04, RHEL 9. Ubuntu 24.04 needs SDP 8.0.2 or later.

- **Windows 11 (x86_64):** the survey host for now. Survey 2026-11-12 found: Software Center 2.0.4; SDP base 8.0.0
  (patchsets up to 8.0.5 = 8.0.5.00390 offered, no 8.0.6); the Pi 5 *quick-start image* 0.4.0 installed (one 12.25 GB
  `rpi5.img`), not the BSP; newest Pi 5 BSP on the server `bsp.hw.raspberrypi_bcm2712_rpi5` 0.4.0.00568; Screen
  board packages for the Pi not yet installed; `devb-nvme` present; `qcc` (gcc 12.2.0) builds aarch64le and x86_64;
  `mkqnximage` 0.5.0 present (qemu, qvm, vbox, vmware types) but QEMU itself is not installed. The 8.0.5 patchset and
  the Pi 5 BSP were being installed afterwards: see `docs/notes/` for the result.
- **Linux build host (planned):** Ubuntu 24.04 x86_64, SDP 8.0.5, QEMU for a target that needs no Pi.

## Target: Raspberry Pi 5 in a Freenove FNK0100K case

Pi 5, 16 GB RAM, 512 GB microSD. Case docs: <https://docs.freenove.com/projects/fnk0100/en/latest/>. The K variant
adds a 4.3in IPS **DSI** screen (CAM/DISP 0) and an M.2 NVMe SSD (128 GB) on the case adapter board over the Pi's
PCIe; a CSI camera on CAM/DISP 1; full-size HDMI0/HDMI1; 3.5 mm audio split from HDMI; speakers; RTC battery; a GPIO
board with 2 fans, ARGB LEDs, an OLED and a thermal sensor, driven over I2C by Freenove's Python service (Raspberry
Pi OS only). Needs a 5.1 V / 5 A supply.

What QNX's Pi 5 BSP documentation says (BSP release notes, Screen board-support notes; read 2026-11-12):

- Labelled **Experimental Software**. Boots from microSD; USB mass storage works.
- Screen graphics: OpenGL ES 3.1 and Vulkan 1.3 via Mesa (`v3d_dri.so`); **2 micro-HDMI displays only**, up to 4K60;
  configs `graphics-rpi5-hdmi1.conf`, `-hdmi2.conf`, `-2xhdmi.conf`, `-virtual.conf`. Touch listed only for a Lilliput
  USB HID monitor.
- No DSI, camera or audio driver is named, so the case screen, camera and speakers are expected not to work under QNX.
  The Freenove fan/OLED service will not run; check the Pi 5 Active Cooler spins before any load.
- Known issues: GL apps crash if run as a different user from Screen and `drm-rpi5` (run both as root); client
  allocated buffers are not supported by `drm-rpi`; use `/dev/dri/`, not `/dev/drm/`.

Keep QNX on a **separate microSD** so the Pi's existing Raspberry Pi OS card stays intact.

## Open questions (phase 1)

- Does QNX 8.0 already ship a VNC server or a Screen remote-display component?
- Which Screen API calls cover display capture (for example `screen_read_display`/`screen_read_window`) and input
  injection (for example `screen_inject_event`), and what privileges do they need?
- Does an existing VNC library (for example LibVNCServer) build for QNX 8.0, or is a small RFB server simpler?
- Does the Pi 5 quick-start image include Screen with HDMI output and USB input out of the box?
- Does `devb-nvme` find the case's NVMe drive on the Pi 5?
