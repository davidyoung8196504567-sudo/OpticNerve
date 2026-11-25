#!/bin/sh
# Start or stop the launcher panel (agent/demo/launch_panel.c) on the target over SSH: a full-screen panel with a tile
# for each app of the quick-start image's launcher configuration (and GTK4 Widget Factory); a click on a tile, from
# a viewer or a test tool, starts that app.
#
#   sh host/tools/launch_panel.sh start          stop any launcher panel, then start one (no time limit)
#   sh host/tools/launch_panel.sh stop           stop the panel; the apps it started keep running
#   sh host/tools/launch_panel.sh stop -apps     also end the apps it started (their process groups, as listed
#                                                 in /dev/shmem/opticnerve-launch.state, which a reboot clears) and
#                                                 list what of them is left
#   sh host/tools/launch_panel.sh status         say whether a panel is running, and which apps it started run
#
# The panel runs as root (the apps run as the image's launcher runs them), from /data/opticnerve/bin/launch_panel,
# installed with the agent by host/tools/opticnerve_deploy.py install; its log is /data/opticnerve/var/launch.log.
# Needs ON_LOGIN_PASSWORD (for sudo), and reaches the target as host/tools/pi_ssh.sh does. Exits 1 if the step failed.
D=$(cd "$(dirname "$0")" && pwd)
. "$D/on_env.sh"
[ -n "$ON_LOGIN_PASSWORD" ] || { echo "launch_panel.sh: set ON_LOGIN_PASSWORD" >&2; exit 1; }
cmd=$1
[ $# -gt 0 ] && shift
apps=
for a in "$@"; do
	case $cmd$a in
	stop-apps) apps=1 ;;
	*) echo "launch_panel.sh: unknown option $a" >&2; exit 1 ;;
	esac
done
B=/data/opticnerve
S=/dev/shmem/opticnerve-launch.state
# "running": a launch_panel process exists (it is root's, so pidin runs as root)
R="running() { rs pidin -f n | grep -q launch_panel; }"
# the image's library path, for the panel's icon codec
L=/proc/boot:/system/lib:/system/lib/dll
case $cmd in
start) sh "$D/pi_ssh.sh" "$R
	test -x $B/bin/launch_panel || { echo 'no launch_panel in $B/bin: install the agent'; exit 1; }
	rs slay -f launch_panel >/dev/null 2>&1; sleep 1
	rs sh -c 'cd / && LD_LIBRARY_PATH=$L $B/bin/launch_panel -daemon < /dev/null >> $B/var/launch.log 2>&1'
	sleep 2
	running && echo 'launcher panel started' || { rs tail -5 $B/var/launch.log; exit 1; }" ;;
stop) sh "$D/pi_ssh.sh" "$R
	rs slay -f launch_panel >/dev/null 2>&1; sleep 1
	running && { echo 'launcher panel still running'; exit 1; }
	echo 'launcher panel stopped'
	# /dev/shmem is anyone's to write in: only a state file of root's (the panel's) is acted on
	if [ -n '$apps' ] && rs test -s $S && [ \"\$(rs ls -l $S | awk '{print \$3}')\" = root ]; then
		rs cat $S > /tmp/onl-groups.\$\$
		while read g name; do
			case \$g in *[!0-9]* | '' | 0 | 1) continue ;; esac
			rs sh -c 'kill -s TERM -- -\$1' _ \$g && echo \"ended \$name (group \$g)\"
		done < /tmp/onl-groups.\$\$
		sleep 2
		: > /tmp/onl-left.\$\$
		while read g name; do
			case \$g in *[!0-9]* | '' | 0 | 1) continue ;; esac
			# gone only when kill says so; any other failure counts as still running
			if ! rs sh -c 'kill -0 -- -\$1' _ \$g 2>&1 | grep -q 'No such process'; then
				echo \"left of \$name: group \$g still runs\"; echo \"\$g \$name\" >> /tmp/onl-left.\$\$
			fi
		done < /tmp/onl-groups.\$\$
		rs sh -c 'cat \$1 > $S' _ /tmp/onl-left.\$\$   # only what still runs stays listed
		rm -f /tmp/onl-groups.\$\$ /tmp/onl-left.\$\$
	fi
	true" ;;
status) sh "$D/pi_ssh.sh" "$R"'
	running && echo "launcher panel running" || echo "no launcher panel"
	rs cat '$S' 2>/dev/null | while read g name; do echo "started by it: $name (group $g)"; done' ;;
*) sed -n '5,12p' "$0" >&2; exit 2 ;;
esac
