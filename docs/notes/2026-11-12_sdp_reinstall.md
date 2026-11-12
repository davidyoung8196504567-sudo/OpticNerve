# QNX SDP 8.0 reinstall with Raspberry Pi 5 packages (2026-11-12)

Follow-up to [2026-11-12_win11_survey.md](2026-11-12_win11_survey.md). The original SDP at `%USERPROFILE%\qnx800`
disappeared from disk mid-morning (cause not found) while the Software Center still listed it. The Software Center
was cleaned up and the SDP reinstalled through the Software Center GUI. This note records what is installed now,
checked read-only from the Software Center CLI (`-offline -listInstalledRoots`, `-exportPatchSetText`).

## Headline

- **Use `%USERPROFILE%\qnx800_2`.** It has the SDP plus the Pi 5 BSP, the Pi 5 Screen board packages and quickstart
  image 0.5.0. `%USERPROFILE%\qnx800` was a plain SDP with no Pi 5 packages. Every package in it is also in
  `qnx800_2` at the same version (`qnx800_2` adds 36), so it is being removed.
- **Up to date.** No installed package has a newer version in the server list cached on 2026-11-11.
- **Newest version of every package, without a patchset package.** The base package and qconfig say 8.0.0, and no
  `patchset80x` package is installed. But every installed package is the newest on the server, and their GA tags go
  up to 8.0.5 (see "GA tags" below). This is not one of QNX's named 8.0.x levels, which are defined by the
  `patchset80x` packages; see [../CONFIG_MANAGEMENT.md](../CONFIG_MANAGEMENT.md).
- **No downgrades.** Compared with this morning's package list, the only version changes in `qnx800_2` are the three
  quickstart packages going from 0.4.0 to 0.5.0. Core components are unchanged (for example microkernel.core
  2.6.0.00145, screen.base 4.3.0.00572, mkqnximage 0.5.0.00132).

## Installations

| | `%USERPROFILE%\qnx800` | `%USERPROFILE%\qnx800_2` |
|---|---|---|
| qconfig id | `com.qnx.qnx800_1` | `com.qnx.qnx800_2` |
| SDP | `com.qnx.qnx800` 8.0.0.00141 | `com.qnx.qnx800` 8.0.0.00141 |
| Packages | 222 | 258 |
| Size on disk | 4.7 GB | 8.2 GB |
| Pi 5 packages | none | see below |

## Pi 5 packages in `qnx800_2`

Installed as root packages:

| Package | Version |
|---|---|
| `bsp.hw.raspberrypi_bcm2712_rpi5` | 0.4.0.00568T202606081017L |
| `quickstart.rpi5` (+ `.0`, `.1`, `.2`) | 0.5.0.00050T202609021847L |
| `target.screen.board.rpi` | 1.0.0.00135T202511211618L |
| `target.screen.board.rpi.server` | 1.0.0.00083T202511201325L |
| `target.pci.hw.bcm2712` | 3.0.0.03044T202509231229L |
| `target.driver.cypress_dhd_sdio_rpi5_2_11` (Wi-Fi) | 0.3.0.00006T202607281203L |
| `target.sf.hw.rpi5`, `target.sf.platform.broadcom.rpi5` (camera) | 0.3.0.00207T202507171524L |

Installed as dependencies, among 33 new packages: `screen.board.rpi.mesa` and `common_mesa` 25.1.1.00046,
`screen.board.drm`, `common_drm`, `libdrm.v2`, `sf.camera.imx708`, `sf.camapi`, `sf.sensor`, `usb.hcd.xhci.dwc3`,
`net.wpa_supplicant_2.11.iosock`, and Linux host tools (mkifs, mkxfs, dumpifs, python3).

Files of note:

- BSP archive: `qnx800_2\bsp\BSP_raspberrypi-bcm2712-rpi5_be-800_SVN1028919_JBN568.zip` (25 MB, not extracted).
- Screen configs: `qnx800_2\target\qnx\aarch64le\usr\lib\graphics\drm-rpi5\graphics-rpi5-{hdmi1,hdmi2,2xhdmi,virtual}.conf`.
  These were missing this morning.
- Quickstart image: `qnx800_2\images\rpi5\qnx_sdp8.0_rpi5_quickstart_20260902.tar.gz.{0,1,2}` (2,185,820,685 B in
  all), plus `README.md` and `unpack_rpi5_image.sh`. Not extracted.
- The camera stack now includes an IMX708 sensor package (`sf.camera.imx708`). This morning only OV5640 and
  OV10635 libraries were found.

## GA tags and status

The Software Center GUI shows a tag such as "Stable GA8.0.5" next to each package. These come from two fields in the
cached server data (`%USERPROFILE%\.qnx\swupdate\server-cache\qnx800-q2-repo-*\packages.json`):
`com.qnx.marketingVersion` (the GA release a package version shipped in) and `com.qnx.status` (stable, validated or
experimental). For the 258 packages in `qnx800_2`:

| marketingVersion | Packages | Status |
|---|---|---|
| GA8.0.5 | 96 | stable |
| GA8.0.3 | 13 | stable |
| GA8.0.2 | 35 | stable |
| GA8.0.1 | 12 | stable |
| GA8.0.6 | 2 (`target.qplms`, `target.qplms.utils`) | stable |
| no GA tag ("8.0 Build N - date") | 100 | 78 stable, 21 experimental, 1 validated (the Pi 5 BSP) |

A GA8.0.2 tag means the package has not changed since 8.0.2, not that it is out of date. The 21 experimental packages
are the quickstart image (4 parts), the Cypress Wi-Fi drivers and `dhd_utils` (4), the sensor framework and camera
stack (7), `screen.board.drm` and `screen.board.intel`, `wpa_supplicant_2.11.iosock`, `osr.libyuv`, `mm.aoi` and
`mm.mmf.core`. The Pi 5 Screen packages `screen.board.rpi`, `.rpi.mesa` and `.rpi.server` are stable.

## File integrity

`python sdp/check_sdp.py` (see [../CONFIG_MANAGEMENT.md](../CONFIG_MANAGEMENT.md)) hashed all 24,117 packaged files
in `qnx800_2` against the SHA-512 values in QNX's package descriptors: none missing, modified or extra. This is
recorded as baseline SDP-BL-001.

The Software Center CLI's `-verifyInstallation` reports 562 "unexpected file" errors, in `qnx800` and `qnx800_2`
alike, all under `target/qnx/usr/share/terminfo`. They are file-name case differences on Windows (`A/a210` on disk,
`a/a210` in the package list), not damage. The GUI's verification reports the installation as clean.

## Toolchain check (`qnx800_2`)

The same `hello.c` as in the survey, built after `call qnxsdp-env.bat`:

| Target | rc | `file` |
|---|---|---|
| `qcc -Vgcc_ntoaarch64le` | 0 | ELF 64-bit LSB pie executable, ARM aarch64, interpreter /usr/lib/ldqnx-64.so.2 |
| `qcc -Vgcc_ntox86_64` | 0 | ELF 64-bit LSB pie executable, x86-64, interpreter /usr/lib/ldqnx-64.so.2 |

The binaries were not run.

## Not checked

- The contents of the BSP zip and the quickstart image, and whether the image boots.
- Whether a patchset can be installed with this licence.
- `qnx800`'s toolchain (only `qnx800_2` was tested).
