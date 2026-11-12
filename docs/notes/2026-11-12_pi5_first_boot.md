# Raspberry Pi 5: first boot of the QNX 8.0 quick-start image (2026-11-12)

## What was booted

- Image: `rpi5.img` from the quick-start package `quickstart.rpi5` 0.5.0.00050 (SDP-BL-005), SHA-256
  `66ba622976ccc69112f389c9b51eee91b5295e7f7da28844d49e7b0bdae5175c`. See
  [../CONFIG_MANAGEMENT.md](../CONFIG_MANAGEMENT.md).
- Written to a microSD with Raspberry Pi Imager (version not recorded). The card was not read back and hash-checked
  before booting, and it can no longer be: on first boot the image grew its `/data` partition to fill the card.
- Boots. On Ethernet it took 10.30.1.116 by DHCP (the address may change).
- Login over SSH as `qnxuser` with the image's documented default password. `qnxuser` is in the groups `sudo` and
  `screen`, among others.

## What the target reports

| Item | Value |
|---|---|
| `uname -a` | `QNX qnxpi 8.0.0 2026/06/05-16:21:14EDT RaspberryPi5 aarch64le` |
| Hostname | `qnxpi` |
| Filesystems | `/` IFS (19 MB), `/boot` FAT 800 MB, `/usr` QNX6 9.9 GB, `/data` QNX6 31 GB (grown on first boot) |
| Open TCP ports | 22 (SSH), 5900 (VNC), 8000 (qconn, the QNX debug agent) |
| Screen | `/dev/screen` present; configs in `/system/etc/system/config` (`img.conf`, `mtouch.conf`, `scaling.conf`) |

`pidin` and `ps` as `qnxuser` show only the user's own processes, so which services run, and under which user, is
not known yet.

## A VNC server is already running

Port 5900 answers `RFB 003.008` and offers one security type: VNC password. The password was not tried and is not
known; the image's documentation may give it.

`/system/bin/vncserv` (also at `/bin` and `/usr/bin`) is byte-identical to the SDP's
`target/qnx/aarch64le/bin/vncserv` from `target.screen.vncserv` 4.2.0.00070 (SHA-256
`fb0f57653e733bb8f1fad8e77c546f615fd2726313ee3247beed8c78004fcdac`).

This bears on phase 1: QNX's own Screen VNC server already runs on the target. Next, with a VNC viewer and the
password: what it shows (the whole display or one window), its frame rate on the Pi 5, and whether it accepts input
(touch, mouse, keys). That decides whether the ON agent builds on `vncserv` or brings its own.

## Not checked

- What the HDMI display shows, and whether a demo app starts.
- Which user `vncserv` runs as, and its command line.
- Whether the QNX Everywhere documentation states the VNC password.
