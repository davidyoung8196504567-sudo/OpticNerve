#!/bin/bash
# Boot the QNX QEMU quickstart guest with an emulated CPU (TCG), on a Linux host without usable KVM: UBUNTU24, whose
# processor is too old for this image under KVM (docs/QEMU_BRINGUP.md, step 7). QEMU_ACCEL=kvm boots it under KVM
# instead (-accel kvm -cpu host), for VMUbuntu24 since VMware passes AMD-V through to it (2026-11-27). The same
# arguments as mkqnximage's runimage script, except the accelerator, the CPU and the CPU count (QEMU_SMP, default:
# the host's), and two for the image's OpenGL Screen (virgl, docs/QEMU_BRINGUP.md step 8): the guest CPU's physical
# address size set to 36 bits (64 GiB, ample for the guest's 4 GB), which that Screen needs on VMUbuntu24, and the
# GPU's display at the Pi 5's 1920 x 1080. QEMU_BIN picks another QEMU (default: the system's
# qemu-system-x86_64), such as the QEMU 10.1.2 with virglrenderer 1.1.1 in /opt/qemu-10.1.2 on VMUbuntu24 (the
# system's QEMU 8.2.2 runs the OpenGL Screen too). The serial console goes to a socket (for scripts) and is logged to
# output/qemu.out; the QEMU window opens on the desktop (whichever display it is on).
#
#   on the host, from ~/opticnerve-work/:
#   setsid nohup ./qemu-run-tcg.sh > qemu-quickstart-20260902/qemu/output/run-tcg.log 2>&1 < /dev/null &
#   QEMU_ACCEL=kvm setsid nohup ./qemu-run-tcg.sh > ... &                  the same under KVM
#   kill $(cat qemu-quickstart-20260902/qemu/output/qemu.pid)          stop
W=/home/tester/opticnerve-work
cd $W/qemu-quickstart-20260902/qemu || exit 1
# The desktop's display, as its gnome-shell has it (:0 or :1, depending on the boot)
D=$(tr '\0' '\n' < /proc/$(pgrep -u "$USER" -x gnome-shell | head -1)/environ 2>/dev/null | sed -n 's/^DISPLAY=//p')
export DISPLAY=${D:-:0} XAUTHORITY=/run/user/$(id -u)/gdm/Xauthority
rm -f output/qemu.out
if [ "$QEMU_ACCEL" = kvm ]; then A="-accel kvm -cpu host,host-phys-bits=off"
else A="-accel tcg,thread=multi -cpu Haswell-noTSX"; fi
exec ${QEMU_BIN:-qemu-system-x86_64} $A,phys-bits=36 -smp ${QEMU_SMP:-$(nproc)} -m 4G \
    -drive file=output/disk-qemu.vmdk,if=ide,id=drv0 \
    -netdev bridge,br=virbr0,id=net0 -device virtio-net-pci,netdev=net0,mac=52:54:00:cf:bf:ed \
    -pidfile output/qemu.pid -kernel output/ifs.bin \
    -chardev socket,id=s0,path=$W/qnx-serial.sock,server=on,wait=off,logfile=output/qemu.out -serial chardev:s0 \
    -object rng-random,filename=/dev/urandom,id=rng0 -device virtio-rng-pci,rng=rng0 \
    -vga none -device virtio-vga-gl,xres=1920,yres=1080 -display sdl,gl=on
