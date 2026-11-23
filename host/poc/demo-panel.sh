#!/bin/sh
# 4) Start the OpticNerve demo panel on the Pi: any running panel is stopped and a fresh one starts (count 0, lamp
# GREEN, for an hour, no pointer marker). Options as host/tools/demo_panel.sh: start [-marker] [SECS], stop, status.
#
#   sh host/poc/demo-panel.sh [start [-marker] [SECS] | stop | status]
POC=$(cd "$(dirname "$0")" && pwd); . "$POC/poc-env.sh"
[ $# -gt 0 ] || set -- start
sh "$ON_REPO/host/tools/demo_panel.sh" "$@"
