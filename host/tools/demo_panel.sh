#!/bin/sh
# Start or stop the demo panel (agent/demo/demo_panel.c) on the target over SSH, so a test starts from a known
# screen: a fresh panel shows count 0, the lamp GREEN and an empty text line.
#
#   sh host/tools/demo_panel.sh start [-marker] [SECS]   stop any panel, then start a fresh one (for SECS seconds,
#                                                        default 3600), without the pointer marker unless -marker
#   sh host/tools/demo_panel.sh stop                     stop the panel
#   sh host/tools/demo_panel.sh status                   say whether a panel is running
#
# Needs ON_LOGIN_PASSWORD (and optionally ON_HOST, ON_LOGIN_USER), as host/tools/pi_ssh.sh does, and the panel
# binary in the login user's home on the target (/data/home/qnxuser/demo_panel). Exits 1 if the step failed.
D=$(cd "$(dirname "$0")" && pwd)
[ -n "$ON_LOGIN_PASSWORD" ] || { echo "demo_panel.sh: set ON_LOGIN_PASSWORD" >&2; exit 1; }
cmd=$1
[ $# -gt 0 ] && shift
marker=-nomarker secs=3600
for a in "$@"; do
	case $a in
	-marker) marker= ;;
	*[!0-9]* | '') echo "demo_panel.sh: unknown option $a" >&2; exit 1 ;;
	*) secs=$a ;;
	esac
done
# "running": a demo_panel process exists (slay -p exits 0 either way, so pidin is used)
R='running() { pidin -f n | grep -q "demo_panel"; }'
case $cmd in
start) sh "$D/pi_ssh.sh" "$R
	cd \$HOME && test -x ./demo_panel || { echo 'no demo_panel in' \$HOME; exit 1; }
	slay -f demo_panel >/dev/null 2>&1; sleep 1
	nohup ./demo_panel $marker $secs > demo.log 2>&1 < /dev/null & sleep 2
	running && echo 'demo panel started (fresh: count 0, lamp GREEN)' || { cat demo.log; exit 1; }" ;;
stop) sh "$D/pi_ssh.sh" "$R"'
	slay -f demo_panel >/dev/null 2>&1; sleep 1; running && { echo "demo panel still running"; exit 1; }; echo "demo panel stopped"' ;;
status) sh "$D/pi_ssh.sh" "$R"'
	running && echo "demo panel running" || echo "no demo panel"' ;;
*) sed -n '5,9p' "$0" >&2; exit 2 ;;
esac
