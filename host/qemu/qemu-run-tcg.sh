#!/bin/bash
# Boot the QNX QEMU quickstart guest on UBUNTU24 with an emulated CPU (TCG): UBUNTU24's processor is too old for this
# image under KVM (docs/QEMU_BRINGUP.md, step 7). The same arguments as mkqnximage's runimage script, except the
# accelerator and the CPU. The serial console goes to a socket (for scripts) and is logged to output/qemu.out; the
# QEMU window opens on the desktop (:0).
#
#   on UBUNTU24, from ~/opticnerve-work/:
#   setsid nohup ./qemu-run-tcg.sh > qemu-quickstart-20260902/qemu/output/run-tcg.log 2>&1 < /dev/null &
#   kill $(cat qemu-quickstart-20260902/qemu/output/qemu.pid)          stop
W=/home/tester/opticnerve-work
cd $W/qemu-quickstart-20260902/qemu || exit 1
export DISPLAY=:0 XAUTHORITY=/run/user/1000/gdm/Xauthority
rm -f output/qemu.out
exec qemu-system-x86_64 -accel tcg,thread=multi -cpu Haswell-noTSX -smp 4 -m 4G \
    -drive file=output/disk-qemu.vmdk,if=ide,id=drv0 \
    -netdev bridge,br=virbr0,id=net0 -device virtio-net-pci,netdev=net0,mac=52:54:00:cf:bf:ed \
    -pidfile output/qemu.pid -kernel output/ifs.bin \
    -chardev socket,id=s0,path=$W/qnx-serial.sock,server=on,wait=off,logfile=output/qemu.out -serial chardev:s0 \
    -object rng-random,filename=/dev/urandom,id=rng0 -device virtio-rng-pci,rng=rng0 \
    -vga none -device virtio-vga-gl -display sdl,gl=on
