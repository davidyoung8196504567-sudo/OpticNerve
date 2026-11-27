#!/bin/sh
# On the QNX QEMU guest, as root: (re)start Screen on the 1920 x 1080 virtual display of graphics-virtual-1080.conf
# (copied next to this script), then drop Screen's privileges to the screen group, as the image's own startup does,
# and start the image's window manager and launcher on it again.
# Stops the OpticNerve agent and demo_panel first, since they are clients of the Screen being replaced.
#
#   sudo sh guest_screen.sh
D=$(cd "$(dirname "$0")" && pwd)
[ -x /data/opticnerve/bin/opticnerve ] && /data/opticnerve/bin/opticnerve stop 2>/dev/null
slay -f demo_panel 2>/dev/null
slay -f screen 2>/dev/null
i=0; while [ -e /dev/screen/command ] && [ $i -lt 20 ]; do sleep 1; i=$((i + 1)); done
# Screen keeps the streams it was started with, so they must not be an SSH session's (it would never end)
screen -u 36:36 -c "$D/graphics-virtual-1080.conf" < /dev/null > /dev/null 2>&1 || exit 1
echo drop_privileges > /dev/screen/command
echo "Screen started: $D/graphics-virtual-1080.conf"
# The image's window manager (it takes Alt+Tab and Alt+Tab+Del, so the agent's key chord) and launcher were clients of
# the Screen replaced above: start them again as the image's /system/etc/startup/full_startup.sh does
slay -f fullscreen-winmgr demolauncher 2>/dev/null
fullscreen-winmgr -s demolauncher,st,gtk < /dev/null > /dev/null 2>&1 &
demolauncher -a /system/etc/desktop_files < /dev/null > /dev/null 2>&1 &
# On the emulated CPU its window comes seconds after it connects to Screen; the launcher panel (started next by the
# agent's boot.sh) must come after it to open on top
i=0; while ! ls /dev/screen | grep -q '^demolauncher-' && [ $i -lt 30 ]; do sleep 1; i=$((i + 1)); done
sleep 5
echo "window manager and launcher started"
