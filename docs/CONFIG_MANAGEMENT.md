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
| QNX SDP packages (258: SDP 8.0, Pi 5 BSP, Screen board support, quickstart image, drivers) | `sdp/packages.lock`: id, exact version, SHA-256 of the package's file list | baseline SDP-BL-001 |
| Every installed SDP file (24,117 files, 8.08 GB) | the SHA-512 of each file in QNX's own package descriptors, which the lock pins; summarised by `fingerprint` in `sdp/BASELINE.txt` | baseline SDP-BL-001 |
| Toolchain (qcc, gcc 12.2.0 for aarch64le and x86_64) | part of the SDP packages; `check_sdp.py` also builds a test program for both targets | baseline SDP-BL-001 |
| Pi 5 BSP archive and quickstart image tarballs | they are SDP package files, so covered by the two rows above | baseline SDP-BL-001 |
| QNX Software Center | recorded: 2.0.4 (v202501021438). Not pinned by the check. | recorded |
| Host OS and Python | recorded: Windows 11 10.0.26300, Python 3.14.7 | recorded |
| Target image written to the Pi 5 card | to come: SHA-256 of `rpi5.img` and of every file added to it | not yet |
| Oculix on the host | to come: exact release and its hash | not yet |
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
- `*.pyc` files are ignored, since running the SDP's Python tools creates them.
- QNX SDP 8.0 build hosts are Windows and Linux only (no macOS). Only a Windows host has been recorded.
  `packages.lock` marks the 29 Windows-only host packages (`win32`), so a Linux host is expected to lack those
  and to have its own host packages instead. The first run on such a host will list them as "not in the lock";
  review and record them then.

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

SDP-BL-001 is "the newest version of every package on 2026-11-12". It is not one of QNX's named levels: QNX defines
8.0.2 to 8.0.5 by the `patchset80x` recipe packages, and none is installed. Describe it as "QNX SDP 8.0, baseline
SDP-BL-001", not as "8.0.5". What QSC is documented to do, and where to report problems with it, is in
[notes/2026-11-12_qsc_documentation.md](notes/2026-11-12_qsc_documentation.md).

## Reproducing the baseline on a new machine

Not yet tried on a second machine.

1. Install QNX Software Center 2.0.4, sign in with the myQNX account that holds the QNX Everywhere licence.
2. Create an SDP 8.0 installation. The path is free; pass it to the check or set `QNX_SDP_DIR`.
3. Install exactly the packages and versions in `sdp/packages.lock`. `sdp/qsc-patchset.txt` is the same list as a
   Software Center text patch set, which is QNX's documented way to give a team identical installations: a recipe of
   packages and exact versions, with no files in it. The documented CLI sequence is `-mirror`, then
   `-importAndInstall sdp/qsc-patchset.txt -destination <path>`, both with `-myqnx.user` and `-myqnx.password`.
   The QNX docs warn that the password is logged in clear text and recommend an `@options_file`. Use the CLI for
   the import: in the GUI, a patch set naming a package the account cannot get fails silently (known issue
   J2993519). This import has not been tested here.
4. Run `python sdp/check_sdp.py <path>`. The machine is ready when it prints `MATCHES THE BASELINE`.

Risk: this depends on QNX keeping these exact versions on its server. The server currently lists older versions
alongside newer ones (for example four versions of the Pi 5 quickstart image).

## Baseline history

| Baseline | Date | Commit | Change |
|---|---|---|---|
| SDP-BL-001 | 2026-11-12 | the commit that adds this row | First baseline. SDP 8.0, newest version of every package (GA tags up to 8.0.5, two at 8.0.6), no patchset package; Pi 5 BSP 0.4.0.00568; quickstart image 0.5.0.00050; Screen board rpi 1.0.0.00135. 258 packages, 24,117 files, fingerprint `a49ccaf994b541e6`. Details: [notes/2026-11-12_sdp_reinstall.md](notes/2026-11-12_sdp_reinstall.md). |
