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

**How we boot it on UBUNTU24:** `~/opticnerve-work/qemu-run-tcg.sh`, the same QEMU arguments as `runimage` except
`-accel tcg,thread=multi -cpu Haswell-noTSX -smp 4`, with the serial console on a socket
(`~/opticnerve-work/qnx-serial.sock`, also logged to `output/qemu.out`) so that it can be used from scripts:

```
setsid nohup ~/opticnerve-work/qemu-run-tcg.sh > output/run-tcg.log 2>&1 < /dev/null &
virsh -c qemu:///system net-dhcp-leases default      # the guest, MAC 52:54:00:cf:bf:ed
kill $(cat output/qemu.pid)                           # stop
```

The guest boots in under a minute and took 192.168.122.146 from libvirt's DHCP. Its users and passwords are in the
login banner and the QNX guide (we use `qnxuser`, which may `sudo`). `sshd` starts about 40 s after boot; before
that port 22 refuses. `vncserv` listens on 5900.

## 8. Screen on a virtual display, and the agent

The image starts Screen with `graphics-virtio-virgl.conf`, the virtio GPU with 3D (virgl), for which the guide
asks for QEMU 10 and virglrenderer 1.1.1 built from source. UBUNTU24 has Ubuntu's QEMU 8.2.2 and virglrenderer
1.0.0, so there we run Screen on the image's own `graphics-virtual-display.conf` instead: a 1280 x 768 display in
memory, no GPU. As root on the guest:

```
screen -u 36:36 -c /usr/share/screen/graphics-virtual-display.conf
echo drop_privileges > /dev/screen/command
```

The agent and the demo panel, built on the Windows host for x86_64 (`agent/build/x86_64/`, SDP-BL-007), copied to
the guest from UBUNTU24 with `scp` (the guest's network is reached only from UBUNTU24), 2026-11-20 20:24:

```
printf 'PASSWORD' | ./opticnerve passwd -passfile=$D/vncpasswd      # as root, D=/data/home/qnxuser/ontest
./opticnerve serve -passfile=$D/vncpasswd < /dev/null > /dev/null 2>> $D/serve.log
./demo_panel &                                                      # as qnxuser
```

Result: `opticnerve 0.1.0: display 1 (1280x768), port 5901`. From UBUNTU24, `host/tools/vnc_snapshot.py
192.168.122.146:5901` returned the demo panel (cropped: the panel is laid out for 1920 x 1080), and
`host/tools/vnc_input.py ... click:370,740` twice took its count from 0 to 2. So the x86_64 agent serves the screen
and injects input on the QEMU target, as on the Pi 5.

## 9. Reach the guest from the Windows host

The guest's network (192.168.122.0/24) is NAT behind UBUNTU24, so the Windows host reaches it through an SSH tunnel
to UBUNTU24; nothing changes on either side. From Git Bash on the Windows host:

```
ssh -f -N -o ExitOnForwardFailure=yes -o ServerAliveInterval=30 \
    -L 127.0.0.1:15901:192.168.122.146:5901 -L 127.0.0.1:12222:192.168.122.146:22 tester@10.30.1.132
python host/tools/vnc_snapshot.py 127.0.0.1:15901 PASSWORD out.png     # the agent on the guest
ssh -p 12222 qnxuser@127.0.0.1                                          # the guest's shell
```

The forwards listen on 127.0.0.1 only. Checked 2026-11-20: a capture of the demo panel through 15901, and the
guest's SSH banner through 12222. Stop the tunnel by ending its `ssh` process.

Still to do: run the test suites against the guest, and choose a demo panel size for 1280 x 768.

