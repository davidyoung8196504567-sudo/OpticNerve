#!/bin/sh
# Start the QNX QEMU guest in the background unless one runs, from the desktop's autostart entry
# (host/qemu/opticnerve-qemu.desktop) when tester logs in on the Linux host. Copy next to qemu-run-tcg.sh, in
# ~/opticnerve-work/ (docs/QEMU_BRINGUP.md, step 7).
W=$(cd "$(dirname "$0")" && pwd)
pgrep -f '^qemu-system-x86_64 .*disk-qemu' > /dev/null && exit 0
cd "$W" && exec setsid ./qemu-run-tcg.sh > qemu-quickstart-20260902/qemu/output/run-tcg.log 2>&1 < /dev/null
