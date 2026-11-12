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

Port 5900 answers `RFB 003.008` and offers one security type: VNC password. The login message (`/etc/motd`) says the
VNC server runs by default with the default password `qnxuser`, and that it can be changed with
`sudo vncserv -password=<new-password>`.

First frame, taken with `host/tools/vnc_snapshot.py` (view-only; it sends no input): authentication OK, framebuffer
1920 x 1080, server pixel format 16 bpp, desktop name `RaspberryPi5`. One full frame in raw encoding took 0.29 to
0.41 s over Ethernet and Wi-Fi. The HDMI screen showed the terminal (`st`) with the login message:

![First frame from the Pi 5 over VNC](img/2026-11-12_pi5_vnc_first_frame.png)

The login message also says the image has the `apk` package manager (`sudo apk add`) for adding target software.
Anything added that way changes the target, so it has to be recorded like an SDP change.

`/system/bin/vncserv` (also at `/bin` and `/usr/bin`) is byte-identical to the SDP's
`target/qnx/aarch64le/bin/vncserv` from `target.screen.vncserv` 4.2.0.00070 (SHA-256
`fb0f57653e733bb8f1fad8e77c546f615fd2726313ee3247beed8c78004fcdac`).

## The bundled samples over VNC

David started each sample from the login message on the Pi (display on HDMI0), and frames were taken with
`host/tools/vnc_snapshot.py` while it ran. Every one came through, with consecutive frames differing, so the capture
is live:

| Sample | Rendering | Captured | Notes |
|---|---|---|---|
| `st` | terminal | yes | the first frame above |
| `gles2-gears` | OpenGL ES 2 | yes | all three gears, the blue one on the blue background included; shading changes with the light angle |
| `gles2-maze` | OpenGL ES 2, textures and shaders | yes | brick texture detail sharp |
| `vkcubepp` | Vulkan | yes | the cube shows LunarG's logo, a third-party trademark: keep it out of the demo video |
| `camera_example3_viewfinder` | camera / video | yes | a moving colour-bar test pattern: no camera driver is configured for the case's CSI camera |

![The samples over VNC](img/2026-11-12_pi5_vnc_samples.png)

Each one-shot frame (new connection, login, full 1920 x 1080 frame uncompressed) took 0.25 to 0.45 s. The server's
native pixel format is 16 bpp; the frames showed no visible banding, but fine gradients may band at that depth.

This bears on phase 1: QNX's own Screen VNC server already runs on the target and shows the whole display, GPU,
Vulkan and video layers included. Next:
its frame rate with incremental updates, and whether it accepts input (touch, mouse, keys). That decides whether
the ON agent builds on `vncserv` or brings its own.

## Not checked

- Whether `vncserv` accepts input events. No input has been sent.
- Frame rate with incremental updates, and CPU load on the Pi while serving.
- Which user `vncserv` runs as, and its command line.
- Whether a stock VNC viewer works with it (none is installed on the Windows host).
