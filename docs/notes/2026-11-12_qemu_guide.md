# QNX's QEMU quick-start guide: what it says (2026-11-12)

Read by a research sub-agent from the QNX Everywhere docs, "Quick Start Target Image (QSTI) for QEMU Guide", all
pages updated 2026-09-24:

- <https://www.qnx.com/developers/docs/qnxeverywhere/com.qnx.doc.target_images/topic/qsti_qemu/about.html>
- <https://www.qnx.com/developers/docs/qnxeverywhere/com.qnx.doc.target_images/topic/qsti_qemu/getting_started.html>
- <https://www.qnx.com/developers/docs/qnxeverywhere/com.qnx.doc.target_images/topic/qsti_qemu/additional_specs.html>
- <https://www.qnx.com/developers/docs/qnxeverywhere/com.qnx.doc.target_images/topic/qsti_qemu/troubleshooting.html>

Check the exact wording at the links before quoting it. David was working through the guide on 2026-11-12; his
results are not recorded here yet.

## Host

- "The QSTI for QEMU runs on Ubuntu 22.04 or 24.04." No other host is mentioned: no Windows, no WSL, nothing about
  running inside another virtual machine.
- Every command uses `--enable-kvm`. No minimum CPU, RAM or disk is stated.

## QEMU

- Ubuntu 24.04 packages for a first run: `sudo apt install qemu-system qemu-utils qemu-user qemu-user-binfmt
  qemu-block-extra libvirt-daemon-system libvirt-clients libguestfs-tools bridge-utils`.
- Recommended: QEMU 10, built from source (`qemu-10.1.2.tar.xz`, configured with `--enable-kvm --enable-opengl
  --enable-sdl --enable-virglrenderer`), with virglrenderer 1.1.1 built from source, Mesa from the `kisak-mesa`
  PPA and the LunarG Vulkan SDK. QEMU 8: Vulkan artifacts, avoid Vulkan. QEMU 6: OpenGL offloading artifacts.
- Without the upgrade: default 720p and no Vulkan.

## Image and running

- x86_64 only (`qemu-system-x86_64`). Package `quickstart.qemu`, pinned in our lock (0.5.0.00050).
- The guide unpacks with `unpack_qemu_image.sh` into `<sdp>/images/qemu/qemu`, inside the SDP. For us: unpack
  outside the SDP, as for the Pi image (see [../CONFIG_MANAGEMENT.md](../CONFIG_MANAGEMENT.md)).
- Start: `source <sdp>/qnxsdp-env.sh`, then in the unpacked folder `mkqnximage --run` (8 CPUs, 4 GB RAM, 720p).
  `mkqnximage --getip` from another terminal in the same folder; `mkqnximage --stop`.
- Console login stated by the guide: root, password root.
- Display: an SDL window with OpenGL (`-display sdl,gl=on`). VNC is not mentioned.
- Network: libvirt bridge `virbr0` (`-netdev bridge,br=virbr0`, fixed MAC `52:54:00:91:01:ea`). Nothing about SSH or
  port forwarding.

## Troubleshooting that matters for WSL

- "The QEMU window manager won't accept keystrokes": on an Ubuntu GNOME desktop with Wayland, some keys (mainly Alt)
  don't reach the QEMU window. The guide's fix is an X11 session ("Ubuntu on Xorg", or `WaylandEnable=false` in
  `/etc/gdm3/custom.conf`).
- WSL shows Linux windows through WSLg, which is itself Wayland-based, and has no Xorg session to switch to. So the
  fix may not be available under WSL (inference, untested).
- Other WSL differences (inference, untested): KVM is nested inside Windows' virtualisation (`/dev/kvm` is present
  in our Ubuntu 24.04 WSL); `virbr0` is a bridge inside WSL, so the target may not be reachable from Windows, where
  Oculix runs.
- For OpticNerve the QEMU window matters mainly for setup: if the QEMU image also runs `vncserv`, the target screen
  goes over VNC like the Pi's. Not yet checked.
- If the host has more than 32 GB of RAM and Screen fails to start: set `--cpu host,host-phys-bits-limit=...`.

## Result under WSL (David, 2026-11-12)

QEMU with the QNX image ran under WSL 2 (Ubuntu 24.04) but too slowly to be useful. Decision: run the QEMU target on
a dedicated Ubuntu 24.04 host, which David is installing. That host will need the SDP at the same baseline and its
own entry in `sdp/packages.lock` (the check records it as another `linux` host; how to tell two Linux hosts apart
in the lock is still to be decided).
