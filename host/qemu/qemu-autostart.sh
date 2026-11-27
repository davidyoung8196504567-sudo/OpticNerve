#!/bin/sh
# Start the QNX QEMU guest in the background unless one runs, from the desktop's autostart entry
# (host/qemu/opticnerve-qemu.desktop) when tester logs in on the Linux host. Copy next to qemu-run-tcg.sh, in
# ~/opticnerve-work/ (docs/QEMU_BRINGUP.md, step 7). Under KVM when the host has a usable /dev/kvm (VMUbuntu24, since
# 2026-11-27), else with the emulated CPU; the log is output/run-kvm.log or output/run-tcg.log.
W=$(cd "$(dirname "$0")" && pwd)
pgrep -f '^qemu-system-x86_64 .*disk-qemu' > /dev/null && exit 0
if [ -r /dev/kvm ] && [ -w /dev/kvm ]; then export QEMU_ACCEL=kvm; else QEMU_ACCEL=tcg; fi
cd "$W" && exec setsid ./qemu-run-tcg.sh > qemu-quickstart-20260902/qemu/output/run-$QEMU_ACCEL.log 2>&1 < /dev/null
