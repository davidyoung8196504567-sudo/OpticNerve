#!/bin/sh
# On the QNX QEMU guest: start Screen on the 1920 x 1080 virtual display, then the OpticNerve agent and the launcher
# panel, at every boot of the guest, with no step by hand (docs/QEMU_BRINGUP.md, step 8).
#
#   sh host/qemu/guest_boot.sh on [OPTION...]   put guest_screen.sh and its configuration in the guest's
#                                               /data/opticnerve/etc/ (as screen.sh, which the agent's boot.sh runs
#                                               first), then opticnerve_deploy.py boot on with serve's OPTIONs
#                                               (default -logintimeout=0); starts nothing now
#   sh host/qemu/guest_boot.sh off              opticnerve_deploy.py boot off, and take the two files out again
#
# The agent must be installed (opticnerve_deploy.py install agent/build/x86_64/opticnerve). Reaches the guest as
# host/tools/pi_ssh.sh does (through host/qemu/tunnel.sh: ON_HOST=127.0.0.2, ON_LOGIN_PASSWORD the guest's).
Q=$(cd "$(dirname "$0")" && pwd)
D=$(cd "$Q/../tools" && pwd)
. "$D/on_env.sh"
E=/data/opticnerve/etc
S=/tmp/opticnerve-guest-boot
case $1 in
on)
	shift
	[ $# -gt 0 ] || set -- -logintimeout=0
	sh "$D/pi_ssh.sh" "rm -rf $S; mkdir -m 700 $S" || exit 1
	ON_LOGIN_PASSWORD="$ON_LOGIN_PASSWORD" SSH_ASKPASS="$D/pi_askpass.sh" SSH_ASKPASS_REQUIRE=force DISPLAY=x \
		scp -q -o PubkeyAuthentication=no -P "$ON_SSH_PORT" "$Q/guest_screen.sh" "$Q/graphics-virtual-1080.conf" \
		"$ON_LOGIN_USER@$ON_SSH_HOST:$S/" < /dev/null || exit 1
	sh "$D/pi_ssh.sh" "set -e; test -x /data/opticnerve/bin/opticnerve || { echo 'agent not installed'; exit 1; }
		sh -n $S/guest_screen.sh
		rs cp $S/guest_screen.sh $E/screen.sh; rs chown root:root $E/screen.sh; rs chmod 755 $E/screen.sh
		rs cp $S/graphics-virtual-1080.conf $E/; rs chown root:root $E/graphics-virtual-1080.conf
		rs chmod 644 $E/graphics-virtual-1080.conf
		rm -rf $S
		echo \"installed $E/screen.sh, SHA-256 \$(openssl dgst -sha256 -r $E/screen.sh | cut -c1-64)\"" || exit 1
	python "$D/opticnerve_deploy.py" boot on "$@"
	;;
off)
	python "$D/opticnerve_deploy.py" boot off || exit 1
	sh "$D/pi_ssh.sh" "rs rm -f $E/screen.sh $E/graphics-virtual-1080.conf; echo 'removed $E/screen.sh and its configuration'"
	;;
*)
	echo "usage: sh host/qemu/guest_boot.sh on [OPTION...] | off" >&2; exit 1 ;;
esac
