# Configuration management

Written 2026-11-12. The aim: any result in this repo can be reproduced on another machine, and any change to the
QNX software on a build host is deliberate, recorded and checked.

## The rule

**A build host is only used when `python sdp/check_sdp.py` prints `MATCHES THE BASELINE`.** The baseline is the set
of files in [`sdp/`](../sdp/), under git. Changing the QNX software means changing the baseline, in one commit, by
the procedure below.

## What is controlled

| Item | Pinned by | Status |
|---|---|---|
| QNX SDP packages (SDP 8.0, Pi 5 BSP, Screen board support, quickstart image, drivers): 294 on Windows, 286 on Linux, 265 of them common | `sdp/packages.lock`: id, exact version, SHA-256 of the package's file list, and the build hosts it is installed on | baseline SDP-BL-005 |
| Every installed SDP file (Windows: 24,743 files, 11.62 GB; Linux: 22,862 files, 10.83 GB) | the SHA-512 of each file in QNX's own package descriptors, which the lock pins; summarised per host by `fingerprint` in `sdp/BASELINE.txt` | baseline SDP-BL-005 |
| Toolchain (qcc, gcc 12.2.0 for aarch64le and x86_64) | part of the SDP packages; `check_sdp.py` also builds a test program for both targets | baseline SDP-BL-001 |
| Pi 5 BSP archive and quickstart image tarballs | they are SDP package files, so covered by the two rows above | baseline SDP-BL-001 |
| QNX Software Center | recorded: 2.0.4 (v202501021438). Linux installer `qnx-setup-2.0.4-202501021438-linux.run`, SHA-256 `5695a0ec8a1fd91840753b2cba86a9325f6052d4f69c757166cc61b09bcbe507`. Not pinned by the check. | recorded |
| Host OS and Python | recorded: Windows 11 10.0.26300, Python 3.14.7. Linux: UBUNTU24, a dedicated Ubuntu 24.04 LTS machine (10.30.1.132, kernel 7.0.0-43-generic), Python version to record; it replaces Ubuntu 24.04 under WSL 2 (Python 3.12.3), which is no longer used (see below) | Windows recorded; UBUNTU24 to record |
| OCR on the host | Tesseract 5.5.0, unpacked by OculiX 4.0.0 on first use (via its Legerix loader) to `C:\Users\root\AppData\Local\legerix\5.5.0\` (`tessdata`, `win32-x86-64`); English data `eng.traineddata` from the pinned OculiX jar. Used by `host/spikes/ocr/OcrScreen.java`. | recorded 2026-11-13 |
| Webcam and recording (host) | Webcam: Logitech C922 Pro Stream Webcam (USB 046D:085C, Logitech driver 1.4.40.0 of 2021-04-26), pointed at the Pi 5's HDMI monitor. OBS Studio 33.0.1 (updated by David from 32.2.2 on 2026-11-13), obs-websocket 5.7.6, server on localhost:4455 with authentication; controlled by `host/tools/obs_control.py`. Settings made on 2026-11-13 at David's request: canvas and output 1920 x 1080 at **30 fps** (was 60); scene "Scene" = the webcam (`Video Capture Device`: 1920 x 1080, MJPEG, 30 fps; was the 640 x 480 default), fitted to the canvas; scene "Scene 2" = `LaptopDisplay` (the laptop's whole 2560 x 1440 display, scaled to fit; before, only its top-left 1920 x 1080 showed); **Desktop Audio and Mic/Aux muted** (unmute only to record target audio or narration). Recording: simple mode, quality "High Quality, Medium File Size", NVIDIA NVENC H.264, hybrid MP4, to `D:\Videos\OBS_recording.mp4` (fixed name, overwrite on): `obs_control.py start` refuses while that file exists, and `stop LABEL` renames it to `OpticNerve_<UTC>_<LABEL>.mp4`. About 5.8 Mbit/s (3.6 MB for 5 s). Direct webcam frames without OBS: `host/tools/webcam/WebcamGrab.java` (needs `OPENCV_VIDEOIO_MSMF_ENABLE_HW_TRANSFORMS=0`). | recorded 2026-11-13 |
| VNC target: MacBook M4 Pro | Apple M4 Pro, macOS 27.2.1 (build 26B131), 10.30.1.128; one built-in Retina display, "looks like" 1352 x 878 at 2x (2704 x 1756 backing); application firewall off. **Change, 2026-11-13** (David, in the GUI, between 22:19 and 22:27 EST; verified 22:27 EST by the ON-MacBookM4Pro session): System Settings > General > Sharing > Screen Sharing off to on; in its (i) options "VNC viewers may control screen with password" on, with a password David set (not recorded; at most 8 characters count); access limited to David's account, to be confirmed by David. Effect: TCP 5900 on all interfaces, `com.apple.screensharing` enabled; banner `RFB 003.889`, security types 30, 33, 36, 31, 32, 2, 35 (2 is VncAuth, the one OculiX uses; checked from Windows without logging in). VncAuth is not encrypted on the LAN. Not changed: Remote Login (port 22) stays off, firewall stays off, nothing installed. **Undo:** first turn off "VNC viewers may control screen with password" in (i), which removes the stored password, then turn Screen Sharing off; check from another host that port 5900 refuses. Purpose: target for read-only OculiX probes from the Windows host and UBUNTU24; input tests only in a slot David schedules. **Stays on** (David, 2026-11-13, relayed by the ON-MacBookM4Pro session): the Mac is on the private LAN and more Mac-hosted testing is coming; no end date; undo steps unchanged. | Mac recorded |
| Target image written to the Pi 5 card | QNX quick-start image 20260902 (package `quickstart.rpi5` 0.5.0.00050, pinned in the lock). Unpacked on 2026-11-12 outside the SDP to `C:\Users\root\opticnerve-work\rpi5-quickstart-20260902\rpi5\rpi5.img`: 12,556,199,936 bytes, SHA-256 `66ba622976ccc69112f389c9b51eee91b5295e7f7da28844d49e7b0bdae5175c`. MBR with three partitions: FAT32 boot (0.78 GiB), QNX6 (10.00 GiB), QNX6 (0.91 GiB); needs a microSD of 16 GB or more. Files added to the card later: to come | image recorded |
| Oculix on the host | OculiX **4.0.0** (latest stable; 4.1.0-rc1 is a pre-release), <https://github.com/oculix-org/Oculix/releases/tag/v4.0.0>. Windows host: `oculixide-4.0.0-windows.jar` (130,253,369 bytes, SHA-256 `7eda19dff0e14ce462b535b788a7b48afda2ca163f33307579541706dab6cd0e`) and `oculixapi-4.0.0-windows.jar` (69,511,190 bytes, SHA-256 `b8ff76a74e582c7d36df2ca1dfb3f09bf564395d47502a9da5db84a304f70638`), both equal to GitHub's published digests; downloaded 2026-11-12 to `C:\Users\root\opticnerve-work\oculix-4.0.0\` with a `SHA256SUMS` file. Linux jars not yet downloaded (published digests: `oculixapi-4.0.0-linux.jar` `bfebf7841eeca171e632739c00eeba879683c32d7530e9af624ae447010c9554`, `oculixide-4.0.0-linux.jar` `95bd353ddd92af9779845c83c36646bc5e5ca5e767196818615444c02d49533d`). **Pin by hash, not by version:** the release was published 2026-07-09, but its jars were replaced on 2026-08-26 (21:28 UTC, a minute before 4.1.0-rc1) and are still named and versioned 4.0.0, so a 4.0.0 jar downloaded before then is a different build. Java on the Windows host: Temurin 25.0.4+7. | Windows recorded |
| This repo | git commit | - |

## How the check works

`sdp/check_sdp.py` is read-only and does not rely on the Software Center being right. Each QNX package installs a
descriptor (`.packages/metadata/<id>/<version>/AssetDescriptor.json`) listing the size and SHA-512 of every file it
owns. The check:

1. compares the Software Center's package list with the descriptors on disk (a cross-check of the Software Center);
2. hashes every packaged file and compares it with its descriptor; reports missing, modified and extra files;
3. builds a test program with qcc for aarch64le and x86_64 and checks the ELF output;
4. compares the package set and each descriptor's SHA-256 with `sdp/packages.lock`, and the overall file
   fingerprint with `sdp/BASELINE.txt`.

It exits 0 only if all four agree. Tested on 2026-11-12: it passes on the baseline installation, reports the 36
missing packages on an SDP without the Pi 5 packages, and on a scratch copy it detects one flipped bit, a deleted
file and a stray file.

Known limits:

- The descriptors come from the same download as the files. The check proves the installation is complete and
  unchanged, and identical to the baseline. It does not verify QNX's signature (`package.sig`) on each package.
- On Windows, symbolic links are installed as copies; the check compares each copy with its link target.
- Windows file names are case-blind. The Software Center CLI's `-verifyInstallation` reports 562 "unexpected file"
  errors on Windows, all under `target/qnx/usr/share/terminfo` (package `target.base.text.ncurses`): the package
  lists `a/a210` and the disk has `A/a210`. Both installations made on 2026-11-12 show the identical 562. They are
  not missing or damaged files, and the GUI's verification reports the installation as clean.
- Two packages can ship the same path with different contents, and the Software Center installs both without
  warning; the later one overwrites the earlier one's files. The check fails on any such path ("conflict").
  Found on 2026-11-12: `target.screen.screen_utils` 4.0.2.00431 ships older copies of 16 files of
  `target.screen.base` and 4 of `target.screen.vncserv`. Do not install it. Removing it deletes the 20 files;
  Verify Installation with repair then restores them from the remaining owners.
- `*.pyc` files are ignored, since running the SDP's Python tools creates them.
- On Linux the Software Center CLI's `-verifyInstallation` reports 0 errors, which confirms the 562 are a Windows
  file-name effect.
- QNX SDP 8.0 build hosts are Windows and Linux only (no macOS). Both are recorded. Each line of `packages.lock`
  ends with the hosts the package is installed on: 265 packages are on both (identical descriptors, so identical
  files), 29 are Windows host tools and 21 are Linux host tools. Everything that goes onto a target is in the
  common 265. `--record` replaces only the entries of the host it runs on, and refuses if a package has the same
  version as on the other host but different contents. `sdp/BASELINE.txt` has one section per host.
- **WSL is no longer part of the project (David, 2026-11-12).** The `[linux]` entries of the baseline were recorded on
  Ubuntu 24.04 under WSL 2; they stay as the specification for the Linux host, which is now UBUNTU24, a dedicated
  Ubuntu 24.04 machine (QNX supports Ubuntu 22.04/24.04 hosts). UBUNTU24 is used only once `check_sdp.py` matches
  those entries; until then the project has one usable build host, Windows. State of UBUNTU24:
  [notes/2026-11-12_ubuntu24_host.md](notes/2026-11-12_ubuntu24_host.md). History rows below that mention WSL are
  left as they were.

Unpack images outside the SDP. QNX's `unpack_<target>_image.sh` extracts into `images/<target>/` inside the SDP,
and the check would then fail on the extracted image as an extra file. Instead run, from a work folder, what the
script does: `cat <sdp>/images/rpi5/qnx_sdp8.0_rpi5_quickstart_20260902.tar.gz.* | tar -xzvf -`, then
`sha256sum rpi5/rpi5.img` and compare with the table above.

## Changing the QNX software

Installs and updates are done by David in the Software Center GUI. Each change follows these steps:

1. Run `python sdp/check_sdp.py`. It must pass first, so the starting point is known.
2. Make one change in the Software Center (one package, or one set that belongs together). Do not use "update all".
3. Run `python sdp/check_sdp.py --record`. It refuses to record an installation with missing or modified files.
4. Read `git diff sdp/`. Every changed line in `packages.lock` must be a change that was intended.
5. Add a row to the baseline history below and commit `sdp/` and this file together.

Experimental packages (21 in SDP-BL-001, including the quickstart image, the Wi-Fi drivers and the camera stack)
are pinned to exact versions like everything else. A newer version on the server is not a reason to update.

Leave the installation property "Install experimental packages" switched on. Turning it off after an experimental
package is installed blocks install, uninstall and repair (QSC 2.0.4 known issue J2812561).

SDP-BL-001 is "the newest version of every package on 2026-11-12". It is not one of QNX's named levels. QNX
defines 8.0.2 to 8.0.5 by the `patchset80x` packages, each a recipe of exact versions (`==`), and none is
installed. Describe it as "QNX SDP 8.0, baseline SDP-BL-001", not as "8.0.5".

Measured against the 8.0.5 recipe (248 packages, from the server list cached on 2026-11-11), SDP-BL-001 has 221
packages at exactly the 8.0.5 version and 7 newer than 8.0.5: `osr.bsdtar` 3.8.8, `target.connectivity.char`
0.2.1, `.devc` 0.2.1, `.io_char` 0.3.0, `target.net.openSSH` 0.5.1, `target.os_services.hid` 0.1.0.00019 and
`target.usb.base` 0.2.0. It adds 30 packages outside the recipe (the Pi 5 BSP, quickstart image, Screen board,
Wi-Fi and camera packages). The recipe's 21 Linux host packages are absent on Windows. Installing the 8.0.5
patch set on this baseline would therefore downgrade those 7 packages.

What QSC is documented to do, and where to report problems with it, is in
[notes/2026-11-12_qsc_documentation.md](notes/2026-11-12_qsc_documentation.md).

## Reproducing the baseline on a new machine

Tried twice on 2026-11-12:

- Ubuntu 24.04 under WSL 2, packages picked by hand in the GUI (newest versions). The first check found two
  packages different from Windows; after correcting those it matched.
- Windows, by patch set import (SDP-BL-005): the CLI's `-importAndInstall` with a text patch set listing the 258
  installed packages plus the 36 new ones (SHA-256 of the file
  `26838ebc2a772b01944b0dbe58186d7ba5b1f3a604fa573056220ea988728450`; it is now `sdp/qsc-patchset-win32.txt`).
  The result was exactly the list: 294 packages, every file hash matching, and QSC's own export afterwards identical
  to the input, `root-package` flags included.

1. Install QNX Software Center 2.0.4, sign in with the myQNX account that holds the QNX Everywhere licence.
2. Create an SDP 8.0 installation. The path is free; pass it to the check or set `QNX_SDP_DIR`.
3. Install exactly the packages and versions in `sdp/packages.lock`. `sdp/qsc-patchset-<host>.txt` is the same list for that host as a
   Software Center text patch set, which is QNX's documented way to give a team identical installations: a recipe of
   packages and exact versions, with no files in it. The documented CLI sequence is `-mirror`, then
   `-importAndInstall sdp/qsc-patchset-<host>.txt -destination <path>`, both with `-myqnx.user` and `-myqnx.password`.
   The QSC 2.0.4 GUI on Windows has no import, only export, so the CLI is the only route. What the trial showed:
   - Close every copy of the Software Center first. With a second copy open, downloads waited about 30 seconds each
     and timed out ("Read timed out"), with no error on screen and no progress.
   - The import does not add to the installation: it uninstalls every installed package (24,633 files) and then
     installs every package in the list. The SDP is unusable for the minute or two this takes, and an interruption
     in between would leave it incomplete. Run the check afterwards in every case.
   - The password ends up in clear text in `~/.qnx/swupdate/qnxsoftwarecenter-<n>.log` and also in
     `~/.qnx/swupdate/.metadata/.log` (the full command line). Delete or clean both after the run. The CLI help
     does not list an `@options_file`, so whether that works is unverified.
   - A patch set naming a package the account cannot get fails silently in the GUI (known issue J2993519); not
     seen here, since all packages were available.
4. Run `python sdp/check_sdp.py <path>`. The machine is ready when it prints `MATCHES THE BASELINE`.

Risk: this depends on QNX keeping these exact versions on its server. The server currently lists older versions
alongside newer ones (for example four versions of the Pi 5 quickstart image).

## Baseline history

| Baseline | Date | Commit | Change |
|---|---|---|---|
| SDP-BL-001 | 2026-11-12 | the commit that adds this row | First baseline. SDP 8.0, newest version of every package (GA tags up to 8.0.5, two at 8.0.6), no patchset package; Pi 5 BSP 0.4.0.00568; quickstart image 0.5.0.00050; Screen board rpi 1.0.0.00135. 258 packages, 24,117 files, fingerprint `a49ccaf994b541e6`. Details: [notes/2026-11-12_sdp_reinstall.md](notes/2026-11-12_sdp_reinstall.md). |
| SDP-BL-002 | 2026-11-12 | the commit that adds this row | Linux build host added (`/home/tester/qnx800`, Ubuntu 24.04 under WSL 2): 250 packages, 22,236 files, fingerprint `7ab0954dd91d7e44`. No package changed on Windows (fingerprint still `a49ccaf994b541e6`). Lock format changed to list the hosts per package; `qsc-patchset.txt` renamed to `qsc-patchset-win32.txt`, `qsc-patchset-linux.txt` added. To match Windows, `target.base.libxml2.v13` was added on Linux and `sf.images.rpi5` removed. |
| SDP-BL-003 | 2026-11-12 | the commit that adds this row | Linux only: 31 packages added for the Screen survey and the QEMU target: `target.screen.vncserv` 4.2.0.00070, `screen.device_input_screen`, `screen.demos`, `screen.demos.qnxeverywhere`, `screen.demos_tutorials.source`, `quickstart.qemu` 0.5.0.00050 (4 packages), `target.qemuvirt`, `driver.virtio` (4), `screen.board.vmwgfx`, and as dependencies the font engine, fontconfig, freetype, image codecs, `hypervisor.libhyp` and `screen.base.source`. Linux: 281 packages, 22,708 files, fingerprint `1ac81965a5a5a80a`. Windows unchanged and still to be brought to the same set. `check_sdp.py` now fails on package file conflicts. |
| SDP-BL-004 | 2026-11-12 | the commit that adds this row | Linux only: 5 packages added: `osr.googletest` 1.11.0.00021, `target.utils.gdbserver` 14.2.0.00328, `screen.fonts.dejavu` and `screen.fonts.noto` 1.0.0.00017, `screen.img_codecs.compat` 2.0.0.00166. Linux: 286 packages, 22,862 files, fingerprint `504b9a5add1e0094`. During this change the Software Center uninstalled `target.screen.vncserv` in the same operation (log, 07:17:48, cause not found); the check caught it and the package was reinstalled before recording. Windows unchanged. |
| SDP-BL-005 | 2026-11-12 | the commit that adds this row | Windows brought to the same target packages as Linux by CLI patch set import: the 36 Linux-only target packages of SDP-BL-003 and SDP-BL-004 added. Windows: 294 packages, 24,743 files, fingerprint `36b0ad7f086bd69e`. All 36 have the same descriptors as on Linux. Hosts now differ only in host tools (29 Windows, 21 Linux). Linux unchanged. |
