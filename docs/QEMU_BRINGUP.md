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

Installed versions: QEMU 8.2.2 (`1:8.2.2+ds-0ubuntu1.18`), libvirt 10.0.0 (`10.0.0-2ubuntu8.19`), libguestfs-tools
`1:1.52.0-5ubuntu3`, OVMF `2024.02-2ubuntu0.10`.

Not installed from the guide's list: `qemu-system` (a metapackage for every architecture; only x86_64 is needed),
`qemu-user` and `qemu-user-binfmt` (for running foreign Linux binaries on the host), `bridge-utils` (`brctl`; libvirt
creates its bridge itself). Add them if a later step asks for them. The guide recommends QEMU 10 built from source;
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

To come. Planned, from the guide: unpack the image from the SDP's `quickstart.qemu` package outside the SDP (as for
the Pi 5 image), `source /home/tester/qnx805/qnxsdp-env.sh`, then in the unpacked folder `mkqnximage --run`
(8 CPUs, 4 GB RAM, 720p by the guide's defaults; this host has 4 cores), `mkqnximage --getip` from a second
terminal, and `mkqnximage --stop`. The QEMU window opens on the desktop, so a boot is done in a slot agreed with
David.

## 8. Run the agent

To come. The x86_64 build (`agent/build/x86_64/opticnerve`, built on the Windows host against SDP-BL-007) goes to
UBUNTU24 and from there to the guest: the guest is on the NAT network 192.168.122.0/24, which the Windows host cannot
reach directly. First check whether the image runs Screen and `vncserv`, which the agent needs.
