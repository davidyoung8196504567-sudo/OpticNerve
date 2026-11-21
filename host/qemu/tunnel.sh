#!/bin/sh
# Make the QNX QEMU guest on UBUNTU24 reachable from this host as ON_HOST=127.0.0.2, with its own ports: 22 (SSH) and
# 5901 (the OpticNerve agent). The guest is on libvirt's NAT network behind UBUNTU24, so the ports go through an SSH
# tunnel to UBUNTU24 (docs/QEMU_BRINGUP.md, step 9). Windows answers on all of 127.0.0.0/8 without setup.
#
#   ON_UBUNTU_PASSWORD=... [ON_GUEST=192.168.122.146] sh host/qemu/tunnel.sh        start (in the background)
#   sh host/qemu/tunnel.sh stop
#
# The forwards listen on 127.0.0.2 only. The password comes from the environment only.
D=$(cd "$(dirname "$0")/../tools" && pwd)
if [ "$1" = stop ]; then
    pid=$(ps -ef | awk '/ssh .*-L 127\.0\.0\.2:5901:/ && !/awk/ {print $2; exit}')   # Git Bash's ssh, so its own pid
    [ -n "$pid" ] && kill "$pid" && echo "stopped tunnel (pid $pid)" || echo "no tunnel"
    exit 0
fi
G=${ON_GUEST:-192.168.122.146}
ON_LOGIN_PASSWORD="$ON_UBUNTU_PASSWORD" SSH_ASKPASS="$D/pi_askpass.sh" SSH_ASKPASS_REQUIRE=force DISPLAY=x \
    ssh -f -N -o PubkeyAuthentication=no -o ExitOnForwardFailure=yes -o ServerAliveInterval=30 \
    -L 127.0.0.2:22:$G:22 -L 127.0.0.2:5901:$G:5901 tester@10.30.1.132 < /dev/null && echo "tunnel up: 127.0.0.2:22 and :5901 -> $G"
