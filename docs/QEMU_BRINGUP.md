# QEMU target bring-up on UBUNTU24

How the QNX 8.0 QEMU target was brought up on UBUNTU24, step by step, as it was done on 2026-11-20. The aim is to run
the x86_64 build of the OpticNerve agent on a target that needs no board. Each step says what to run, how to check
it, and how to undo it. Every change is also recorded in [CONFIG_MANAGEMENT.md](CONFIG_MANAGEMENT.md) (row "QEMU
host: UBUNTU24").

Source for the QEMU side: QNX's "Quick Start Target Image (QSTI) for QEMU Guide" (QNX Everywhere documentation,
pages updated 2026-09-24), which names Ubuntu 22.04 and 24.04 as hosts.

Times are the host's local time (EST, UTC-5).

## The host

| | |
|---|---|
| Machine | UBUNTU24, 10.30.1.132, Ubuntu 24.04 LTS, kernel 7.0.0-43-generic, AMD A8-4500M (4 cores, SVM), 15 GiB RAM |
| User | `tester`, in `sudo` |
| QNX SDP | `/home/tester/qnx805`, baseline SDP-BL-009 (SDP 8.0.5 release, the Pi 5 additions, and `quickstart.qemu` 0.5.0.00050 with `target.qemuvirt` 0.2.1.00087) |
| Desktop | GNOME on Xorg (X11), as the guide asks |

## 1. QNX packages (Software Center)

Installed by David in the Software Center GUI, then inventoried and recorded by the ON-Ubuntu24 session:
`quickstart.qemu` 0.5.0.00050 (4 packages) and `target.qemuvirt` 0.2.1.00087. This is baseline SDP-BL-009.

Check: `python3 sdp/check_sdp.py /home/tester/qnx805` prints `MATCHES THE BASELINE`.

## 2. Ubuntu packages (apt)

The guide's list for a first run is
`qemu-system qemu-utils qemu-user qemu-user-binfmt qemu-block-extra libvirt-daemon-system libvirt-clients
libguestfs-tools bridge-utils`. What was run (from `/var/log/apt/history.log`):

| Time | Command | Notes |
|---|---|---|
| 19:16 | `sudo apt install libvirt-clients libguestfs-tools` | pulls in `qemu-system-x86`, `qemu-system-gui`, `qemu-utils`, `qemu-block-extra` and `ovmf` as dependencies |
| 19:23 | `sudo apt install net-tools` | `ifconfig`, `netstat`; not needed by QEMU |
| 19:24 | `sudo apt install libvirt-daemon-system` | the libvirt daemon, the `default` network, and the `libvirt` group |
| 19:33 | `sudo apt install -y openssh-server` | not for QEMU: lets the Windows host's Claude session work on UBUNTU24 (step 6) |
| 19:55 | `sudo apt install bridge-utils` (1.7.1-1ubuntu2) | needed after all: the image's `runimage` and `check-net` scripts call `brctl show` to find `virbr0` |

Installed versions: QEMU 8.2.2 (`1:8.2.2+ds-0ubuntu1.18`), libvirt 10.0.0 (`10.0.0-2ubuntu8.19`), libguestfs-tools
`1:1.52.0-5ubuntu3`, OVMF `2024.02-2ubuntu0.10`.

Not installed from the guide's list: `qemu-system` (a metapackage for every architecture; only x86_64 is needed),
`qemu-user` and `qemu-user-binfmt` (for running foreign Linux binaries on the host). Add them if a later step asks
for them. The guide recommends QEMU 10 built from source;
we start with Ubuntu's 8.2.2.

Check: `qemu-system-x86_64 --version` prints 8.2.2.

Undo: `sudo apt remove libvirt-daemon-system libvirt-clients libguestfs-tools net-tools`, then `sudo apt autoremove`.

## 3. Groups, then log out and in

QEMU needs `/dev/kvm` (group `kvm`), and libvirt's tools need the `libvirt` group:

```
getent group kvm libvirt          # one line for each
sudo usermod -aG kvm,libvirt tester
```

If `usermod` says `group libvirt does not exist`, `libvirt-daemon-system` is not installed yet: its installation
creates the group. That happened here the first time, before the 19:24 install.

A group change applies from the next login only. Log out of the desktop and log in again (19:27 here), choosing
"Ubuntu on Xorg" on the login screen (the gear icon), since the QEMU window is an SDL window with OpenGL on X11.

Check, in a new terminal:

```
groups                     # includes kvm and libvirt
ls -l /dev/kvm             # crw-rw----+ root kvm
echo $XDG_SESSION_TYPE     # x11 (in a desktop terminal)
```

Undo: `sudo gpasswd -d tester libvirt` and `sudo gpasswd -d tester kvm`, then log out and in.

## 4. Start the libvirt daemon

After the install the units were enabled but not running, so `virsh` could not connect
(`Failed to connect socket to '/var/run/libvirt/libvirt-sock'`). Started at 19:36:58:

```
sudo systemctl enable --now libvirtd.socket libvirtd
```

Both start at boot from now on. Note that a plain `virsh` connects to the user's own session (`qemu:///session`),
which has no networks, so `virsh net-list --all` shows an empty list there. The `default` network belongs to the
system daemon:

```
virsh -c qemu:///system net-list --all
```

```
 Name      State    Autostart   Persistent
--------------------------------------------
 default   active   yes         yes
```

If `default` shows as inactive: `sudo virsh net-start default` and `sudo virsh net-autostart default`. Its bridge is
`virbr0`, 192.168.122.1/24, NAT to the LAN; `ip -br addr show virbr0` shows it `DOWN` until a guest is attached.

Undo: `sudo systemctl disable --now libvirtd libvirtd.socket`.

## 5. Let QEMU attach to virbr0 as tester

The guide starts QEMU with `-netdev bridge,br=virbr0`. Run as a normal user, QEMU attaches through
`qemu-bridge-helper`, which only allows bridges listed in `/etc/qemu/bridge.conf` and must run as root. Neither was
set up by the packages. Done at 19:40:

```
sudo mkdir -p /etc/qemu
echo "allow virbr0" | sudo tee /etc/qemu/bridge.conf
sudo chmod 644 /etc/qemu/bridge.conf
sudo chmod u+s /usr/lib/qemu/qemu-bridge-helper
```

Check:

```
cat /etc/qemu/bridge.conf                      # allow virbr0
ls -l /usr/lib/qemu/qemu-bridge-helper         # -rwsr-xr-x root root
```

`/etc/qemu/` did not exist before. A later upgrade of `qemu-system-common` may reset the setuid bit; check it after
upgrades.

Undo: `sudo chmod u-s /usr/lib/qemu/qemu-bridge-helper`, `sudo rm -r /etc/qemu`.

## 6. SSH server (for remote work)

`openssh-server` 9.6p1 (step 2) lets the Claude session on the Windows host log in as `tester` with a password over
the LAN. On Ubuntu 24.04 it is socket-activated: `ssh.socket` is enabled and starts `ssh.service` on the first
connection. The configuration is Ubuntu's default (password login on, root login with a password off); `ufw` is
inactive, so port 22 is open to the LAN.

Check: from another host, `ssh tester@10.30.1.132` asks for the password.

Undo: `sudo apt remove openssh-server`.

## 7. Unpack and boot the QNX image

Unpack outside the SDP (as for the Pi 5 image), with the commands of the package's own `unpack_qemu_image.sh`:

```
mkdir ~/opticnerve-work/qemu-quickstart-20260902 && cd ~/opticnerve-work/qemu-quickstart-20260902
cat ~/qnx805/images/qemu/qnx_sdp8.0_qemu_quickstart_20260902.tar.gz.* | tar -xzf -
```

This gives `qemu/` with `local/` (options and snippets) and `output/` (`ifs.bin`, `disk-qemu.vmdk` and the 48 GB
`disk-qemu` it points to). Everything is already built, so `mkqnximage --run` boots it without a build.

Check the network setup first (it also prints what is missing):

```
cd qemu
TOOL_DIR=~/qnx805/host/common/mkqnximage MKQNXIMAGE_NOPROMPT=1 bash ~/qnx805/host/common/mkqnximage/qemu/check-net qemu-system-x86_64
echo $?      # 0
```

Boot. `--run=-h` sends the serial console to `output/qemu.out` and puts QEMU in the background; the display
(`-vga none -device virtio-vga-gl -display sdl,gl=on` from `output/build/qemu_options`) still opens a window on the
desktop. From a desktop terminal:

```
source ~/qnx805/qnxsdp-env.sh
mkqnximage --run=-h
mkqnximage --getip           # the guest's address on virbr0
mkqnximage --stop
```

From an SSH login the same needs `DISPLAY=:0 XAUTHORITY=/run/user/1000/gdm/Xauthority` (the desktop's X server);
`~/opticnerve-work/qemu-run.sh` does that.

**Host processor.** Use a host with a Haswell-class or newer x86_64 processor (Intel 2013 or later, AMD Zen) for
KVM. UBUNTU24's AMD A8-4500M (2012) is older: on it we boot the guest with QEMU's emulated CPU instead
(`-accel tcg -cpu Haswell-noTSX`, or `-cpu max`). The image's `qemu_options` asks for 8 CPUs (`-smp 8`); on a host
with fewer cores QEMU warns and goes on.

**How we boot it on UBUNTU24:** `~/opticnerve-work/qemu-run-tcg.sh` (a copy of `host/qemu/qemu-run-tcg.sh`), the same QEMU arguments as `runimage` except
`-accel tcg,thread=multi -cpu Haswell-noTSX -smp 4`, with the serial console on a socket
(`~/opticnerve-work/qnx-serial.sock`, also logged to `output/qemu.out`) so that it can be used from scripts:

```
setsid nohup ~/opticnerve-work/qemu-run-tcg.sh > output/run-tcg.log 2>&1 < /dev/null &
virsh -c qemu:///system net-dhcp-leases default      # the guest, MAC 52:54:00:cf:bf:ed
kill $(cat output/qemu.pid)                           # stop
```

**With the host:** to start the guest whenever `tester` logs in to the host's desktop (the host's automatic login,
so at every boot of the host), copy `host/qemu/qemu-autostart.sh` to `~/opticnerve-work/` (next to
`qemu-run-tcg.sh`) and `host/qemu/opticnerve-qemu.desktop` to `~/.config/autostart/`. The script does nothing while
a guest runs. It boots the guest under KVM (`QEMU_ACCEL=kvm`, log `output/run-kvm.log`) when the host's `/dev/kvm`
can be opened, else with the emulated CPU (`output/run-tcg.log`): on VMUbuntu24, KVM since 2026-11-27 (the VM gets
AMD-V from VMware once the Windows hypervisor is off); there, a VM reboot brought the agent up 71 s after the reboot
command. Do not install it on UBUNTU24 as it is: that host has `/dev/kvm`, but its processor is too old for this image
under KVM. Undo: delete the two files.

The guest boots in under a minute and took 192.168.122.146 from libvirt's DHCP. Its users and passwords are in the
login banner and the QNX guide (we use `qnxuser`, which may `sudo`). `sshd` starts about 40 s after boot; before
that port 22 refuses. `vncserv` listens on 5900.

## 8. Screen on a virtual display

The image starts Screen with `graphics-virtio-virgl.conf`, the virtio GPU with 3D (virgl), for which the guide
asks for QEMU 10 and virglrenderer 1.1.1 built from source. UBUNTU24 has Ubuntu's QEMU 8.2.2 and virglrenderer
1.0.0, so there we run Screen on a virtual display in memory, with no GPU: the image's own
`graphics-virtual-display.conf` (1280 x 768) at the Pi 5's 1920 x 1080 instead, `host/qemu/graphics-virtual-1080.conf`.
At the Pi's size, `demo_panel` and the tests' positions and reference pictures are the same as on the Pi.
`host/qemu/guest_screen.sh` stops the agent and `demo_panel`, restarts Screen with that file and drops Screen's
privileges, as the image's startup does, then starts the image's window manager (`fullscreen-winmgr`, which takes
Alt+Tab and Alt+Tab+Del, so the agent's Q+N chord) and its launcher (`demolauncher`) again, since they were clients of
the Screen it replaced, and waits for the launcher's window so that our launcher panel opens over it. On the guest, with both files in `/data/home/qnxuser/qemu/`:

```
sudo sh /data/home/qnxuser/qemu/guest_screen.sh
```

Screen started this way does not survive a reboot of the guest; run the script again after each boot.

**At every boot of the guest**, with no step by hand: once the agent is installed (step 10), from the Windows host
with the tunnel up,

```
sh host/qemu/guest_boot.sh on          # off: undo
```

puts `guest_screen.sh` and its configuration in the guest's `/data/opticnerve/etc/` (as `screen.sh`) and runs
`opticnerve_deploy.py boot on -logintimeout=0`. The agent's `boot.sh` runs `etc/screen.sh` first when there is one,
then starts the agent and the launcher panel as on the Pi. Checked on VMUbuntu24 2026-11-27 with two reboots of the
VM, nothing touched: the agent served the 1920 x 1080 display about 80 s after the reboot command, with the launcher
panel on top, and DemoTest passed.

**What the virtual display cannot run:** it has no OpenGL, so the applications that need it do not start there:
the Developer Desktop (its compositor wants GL), Gears and Maze (`eglGetDisplay` finds no display). Their tiles on the
launcher panel start a process that ends at once. Terminal, Readme, the GTK4 demo, Widget Factory (below) and our demo panel draw without
GL and run. The GL applications need a GL-capable Screen, the image's virgl configuration (above).

**Widget Factory needs compiled GSettings schemas.** On the guest it exited about 3 s after its window opened, with
`GLib-GIO-ERROR: No GSettings schemas are installed on the system`; the guest has the schema sources in
`/usr/share/glib-2.0/schemas/` but no `gschemas.compiled` there. Compile them once, as root on the guest:

```sh
glib-compile-schemas /usr/share/glib-2.0/schemas
```

This writes `/usr/share/glib-2.0/schemas/gschemas.compiled` (79517 bytes, SHA-256 `a9e6d15c...`, the same as the Pi's
file); undo: delete it. Done on VMUbuntu24's guest 2026-11-27: Widget Factory opened from its tile and closed with its
own close button, back to our launcher panel.

**The key chord on the guest** (2026-11-27, after the window manager was added to `guest_screen.sh`): Q held, then N,
switched between our launcher panel and Terminal both ways; Q+N+X closed the app selected by the switch, as Alt+Tab+Del
does on the Pi. After an app closes, the window manager brings the image's launcher to the front; Q+N brings ours back.

A first try on 2026-11-20 20:24 at 1280 x 768, with the agent started by hand, already captured the demo panel
(cropped) and counted two clicks.

**The image's own virgl Screen, from a tester** (Julien, in [his comment of 2026-11-23](https://github.com/oculix-org/Oculix/discussions/505#discussioncomment-19821365)
on OculiX discussion #505): with QEMU 6.2 and KVM on Windows 10 (WSL2 with
nested virtualization), the image's `graphics-virtio-virgl.conf` Screen ran with software OpenGL (llvmpipe).
- At the default 1024 x 768, the agent captured the launcher.
- At 1920 x 1080 (`xres`/`yres` on `virtio-vga-gl`), Screen took 12 to 13 s to start, frames arrived seconds late,
  and DemoTest failed 4 of 6 checks in 31.4 s.
- On our virtual display instead (above), DemoTest passed 5 of 6 in 21.1 s on a 2017 dual-core Core i3; the failure
  was the OCR of the panel's old 5x7 font, since replaced by Roboto.

His tip, which we have not tried: on Windows 11, `nestedVirtualization=true` in `.wslconfig` gives WSL a `/dev/kvm`,
so the guest could run with KVM there instead of the emulated CPU.

## 9. Reach the guest from the Windows host

The guest's network (192.168.122.0/24) is NAT behind UBUNTU24, so the Windows host reaches it through an SSH tunnel
to UBUNTU24; nothing changes on either side. `host/qemu/tunnel.sh` forwards the guest's ports 22 and 5901 to the
same ports on 127.0.0.2, a second loopback address that Windows answers on without setup. With `ON_HOST=127.0.0.2`
the host tools then reach the guest as they reach the Pi, unchanged. From Git Bash on the Windows host:

```
ON_UBUNTU_PASSWORD=... sh host/qemu/tunnel.sh          # start, in the background
sh host/qemu/tunnel.sh stop
```

The forwards listen on 127.0.0.2 only, and the tunnel ends with its `ssh` process (or a reboot of either host).

## 10. The agent and DemoTest on the guest

From the Windows host, with the tunnel up, 2026-11-20 21:10 to 21:20 (`ON_LOGIN_PASSWORD` is the guest's `qnxuser`
password here, not the Pi's):

```
export ON_HOST=127.0.0.2 ON_LOGIN_PASSWORD=...
python host/tools/opticnerve_deploy.py install agent/build/x86_64/opticnerve
python host/tools/opticnerve_deploy.py start
scp agent/build/x86_64/demo_panel qnxuser@127.0.0.2:/data/home/qnxuser/
ssh qnxuser@127.0.0.2 'nohup ./demo_panel > demo.log 2>&1 < /dev/null &'
java -cp "$ON_TIGERVNC_JAR;$ON_OCULIX_JAR" host/demo/DemoTest.java 127.0.0.2 "$ON_VNC_PASSWORD" demo-out/qemu
```

The image has no `sha256sum`, so `opticnerve_deploy.py` checks the installed file with `openssl dgst -sha256`
there. Installed binary SHA-256 `4c5ee62a75b9a849e1ca0c39be5ef00d73dd1d5dd7e6f8d21010498be617e044`; the agent
reported `display 1 (1920x1080), port 5901`.

**DemoTest result: all 6 checks passed in 24.0 s** (connect; find RESET by image, score 1.0000; count 0 and count 3,
each with 0 pixels different from the Pi's reference pictures; LAMP green to red; typed text read back by OCR as
"HELLO PI 5"; the panel left as it started). So the x86_64 agent serves the screen and takes pointer and keyboard
input on the QEMU target as on the Pi 5.

The other suites (DesktopTest, GtkDemoTest, LoginTest, ObstacleCourse) need the image's desktop and applications,
which its startup brings up only together with the virgl Screen of step 8. Starting them on the virtual display has
not been tried yet.

## The same on VMUbuntu24

VMUbuntu24 (10.30.1.104), the VMware virtual machine on the Windows host that replaces UBUNTU24 (row "Linux build
host: VMUbuntu24" in [CONFIG_MANAGEMENT.md](CONFIG_MANAGEMENT.md)), was set up with the steps above on 2026-11-21,
over SSH from the Windows host:

- Step 1: the SDP at SDP-BL-009 already (`check_sdp.py` matches).
- Step 2: `sudo apt install qemu-system-x86 qemu-utils libvirt-daemon-system libvirt-clients bridge-utils git`, the
  same versions as on UBUNTU24 (QEMU 8.2.2, libvirt 10.0.0, bridge-utils 1.7.1); OVMF came with them.
- Steps 3 to 5 as written (`usermod -aG kvm,libvirt tester`, `systemctl enable --now libvirtd.socket libvirtd`,
  `/etc/qemu/bridge.conf`, setuid bridge helper). The `default` network was active at once.
- Step 7: the image unpacked to `~/opticnerve-work/qemu-quickstart-20260902/` (the disk then 80 % full: 20 GB free of
  98 GB). The VM has no KVM (VMware without nested virtualization, as the Windows hypervisor is on), so the guest runs
  with `host/qemu/qemu-run-tcg.sh` too, here with 2 CPUs (`-smp` defaults to the host's count). It booted in about
  40 s and took 192.168.122.146 again.
- Steps 8 to 10 as written: `host/qemu/tunnel.sh` now reaches VMUbuntu24 by default (`ON_QEMU_HOST`), the agent
  (`4c5ee62a...`) reported `display 1 (1920x1080), port 5901`.

DemoTest (`demo_panel -nomarker`), from the Windows host: all 6 checks passed in a normal run, 14.4 s; in `-brief`
mode 10.6 s per run over 5 runs. For comparison, the same test on the Pi 5: 12.7 s and 6.2 s; on UBUNTU24's guest:
15.6 s and 12.1 s. (Measured before 2026-11-23, when the tests still jumped the pointer in `-brief` mode; since
then every run moves it the way a hand does, so runs take longer.)

The agent also builds on VMUbuntu24 (2026-11-21, `make -C agent` with `source ~/qnx805/qnxsdp-env.sh`, at commit ea74ca5): all six binaries (`opticnerve`, `demo_panel`, `core_test` for aarch64le and x86_64) are byte-identical to the Windows host's builds of the same sources, SHA-256 `5fdef8a0...` (aarch64le agent, the one on the Pi) and `4c5ee62a...` (x86_64 agent, the one in the QEMU guest).
