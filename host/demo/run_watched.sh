#!/bin/bash
# Runs a screen test with the target's watcher armed, and stops the test when the watcher records an incident.
#
#   bash host/demo/run_watched.sh NAME -- java -cp "..." host/demo/ObstacleCourse.java HOST VNCPASSWORD [options]
#
# The watcher runs on the target (the Pi 5 fleet peer's /data/home/qnxuser/watch/onwatch.sh): it follows the system
# log, the watched processes and the core dumps, and writes one line per incident to its spool. This script, from the
# Windows host over SSH (host/tools/pi_ssh.sh, which reads ON_LOGIN_PASSWORD):
#   - arms it (onrun.sh start NAME) and disarms it at the end (onrun.sh end NAME);
#   - runs the test in the background, its output in demo-out/NAME.log;
#   - reads the spool every 10 s; a new incident stops the test, saves the screen (demo-out/NAME_alert.png, from the
#     agent on port 5901 with ON_VNC_PASSWORD) and prints the incident.
# The target is ON_HOST (default 10.30.1.116), as for pi_ssh.sh; the watcher's folder, holding onrun.sh and the spool,
# is ON_WATCH_DIR (default the fleet peer's /data/home/qnxuser/watch).
# A second path besides the fleet peer's own alert messages, which depend on its session being able to send them.
# Tell the peer before a run (RUN START / RUN END) all the same: the state file is not the notification.
# Run from the repository root.
set -u
name=$1; shift; [ "${1:-}" = "--" ] && shift
W=${ON_WATCH_DIR:-/data/home/qnxuser/watch}
HOST=${ON_HOST:-10.30.1.116}
ssh_pi() { bash host/tools/pi_ssh.sh "$1" 2>/dev/null | tr -d '\r'; }
# Incidents that count: the course itself closes the web browser after its link obstacles (the watcher reports each
# as "epiphany gone"), so those lines are left out.
COUNT="grep -v -c 'epiphany gone' $W/spool 2>/dev/null || echo 0"

mkdir -p demo-out
n0=$(ssh_pi "$COUNT" | tail -1 | tr -d ' ')
ssh_pi "$W/onrun.sh start $name" | tail -1
echo "start $(date -u +%H:%M:%S) UTC; incidents before: $n0"
"$@" > "demo-out/$name.log" 2>&1 &
pid=$!
stopped=""
while kill -0 $pid 2>/dev/null; do
    sleep 10
    kill -0 $pid 2>/dev/null || break
    n=$(ssh_pi "$COUNT" | tail -1 | tr -d ' ')
    if [ -n "$n" ] && [ "$n" -gt "$n0" ] 2>/dev/null; then
        stopped=yes
        echo "ALERT at $(date -u +%H:%M:%S) UTC: stopping the test"
        ssh_pi "grep -v 'epiphany gone' $W/spool | tail -n $((n - n0))"
        kill $pid 2>/dev/null; sleep 1; kill -9 $pid 2>/dev/null
        python host/tools/vnc_snapshot.py "$HOST:5901" "$ON_VNC_PASSWORD" "demo-out/${name}_alert.png" >/dev/null 2>&1 \
            && echo "screen saved: demo-out/${name}_alert.png"
        break
    fi
done
wait $pid 2>/dev/null; code=$?
ssh_pi "$W/onrun.sh end $name" | tail -1
echo "end $(date -u +%H:%M:%S) UTC; ${stopped:+STOPPED ON ALERT; }test exit $code"
grep -a -E '^(FAIL|ALL PASSED|[0-9]+ FAILED|the whole)' "demo-out/$name.log" | tail -8
