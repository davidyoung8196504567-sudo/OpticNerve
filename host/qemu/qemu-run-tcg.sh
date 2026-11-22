#!/bin/bash
# Boot the QNX QEMU quickstart guest with an emulated CPU (TCG), on a Linux host without usable KVM: UBUNTU24, whose
# processor is too old for this image under KVM, and VMUbuntu24, a VM without nested virtualization
# (docs/QEMU_BRINGUP.md, step 7). The same arguments as mkqnximage's runimage script, except the accelerator, the CPU
# and the CPU count (QEMU_SMP, default: the host's). The serial console goes to a socket (for scripts) and is logged
# to output/qemu.out; the QEMU window opens on the desktop (whichever display it is on).
#
#   on the host, from ~/opticnerve-work/:
#   setsid nohup ./qemu-run-tcg.sh > qemu-quickstart-20260902/qemu/output/run-tcg.log 2>&1 < /dev/null &
#   kill $(cat qemu-quickstart-20260902/qemu/output/qemu.pid)          stop
W=/home/tester/opticnerve-work
cd $W/qemu-quickstart-20260902/qemu || exit 1
# The desktop's display, as its gnome-shell has it (:0 or :1, depending on the boot)
D=$(tr '\0' '\n' < /proc/$(pgrep -u "$USER" -x gnome-shell | head -1)/environ 2>/dev/null | sed -n 's/^DISPLAY=//p')
export DISPLAY=${D:-:0} XAUTHORITY=/run/user/$(id -u)/gdm/Xauthority
rm -f output/qemu.out
exec qemu-system-x86_64 -accel tcg,thread=multi -cpu Haswell-noTSX -smp ${QEMU_SMP:-$(nproc)} -m 4G \
    -drive file=output/disk-qemu.vmdk,if=ide,id=drv0 \
    -netdev bridge,br=virbr0,id=net0 -device virtio-net-pci,netdev=net0,mac=52:54:00:cf:bf:ed \
    -pidfile output/qemu.pid -kernel output/ifs.bin \
    -chardev socket,id=s0,path=$W/qnx-serial.sock,server=on,wait=off,logfile=output/qemu.out -serial chardev:s0 \
    -object rng-random,filename=/dev/urandom,id=rng0 -device virtio-rng-pci,rng=rng0 \
    -vga none -device virtio-vga-gl -display sdl,gl=on
