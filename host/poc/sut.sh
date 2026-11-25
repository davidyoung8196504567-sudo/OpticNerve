#!/bin/sh
# The system under test's side, from this tester: build the OpticNerve agent and the demo panel, and install and
# start them on a QNX target (the Pi 5, or another target at HOST).
#
#   sh host/poc/sut.sh build                 check the SDP against the baseline, then build for aarch64le
#   sh host/poc/sut.sh install [HOST[:PORT]]  stop the agent and the panel, install both, start the agent
#   sh host/poc/sut.sh start|stop|status|undo [HOST[:PORT]]
#
# HOST and PORT are the target's SSH address, default ON_SSH_HOST (default ON_HOST) and ON_SSH_PORT (default 22),
# from the environment or the settings file; a HOST given without :PORT means port 22. The agent listens on port
# 5901 and starts with -logintimeout=0 (no login limit, for testing). install replaces the running agent: it asks
# first when run in a terminal. Record the SHA-256 it prints in docs/CONFIG_MANAGEMENT.md. undo removes the agent
# and the demo panel. Needs ON_LOGIN_PASSWORD (the target's login) and, for install, ON_VNC_PASSWORD.
#
# On a tester without the QNX SDP (macOS), install a prebuilt agent: ON_AGENT_BUILD is a folder holding opticnerve
# and demo_panel for aarch64le, and launch_panel in builds that have it (for example a copy from the share). install
# then checks that they are a build recorded in tests/regression/golden/agent_build.json, and refuses anything else.
POC=$(cd "$(dirname "$0")" && pwd); . "$POC/poc-env.sh"
B=${ON_AGENT_BUILD:-$ON_REPO/agent/build/aarch64le}
# Are the binaries in $B a recorded build? Prints which, or why not (exit 1).
recorded() {
	python3 - "$B" "$ON_REPO/tests/regression/golden/agent_build.json" <<'EOF'
import hashlib, json, sys
from pathlib import Path
b, golden = Path(sys.argv[1]), json.loads(Path(sys.argv[2]).read_text())
got = {n: hashlib.sha256((b / n).read_bytes()).hexdigest() for n in ("opticnerve", "demo_panel", "launch_panel")
       if (b / n).is_file()}   # launch_panel: in builds since ON-AG-003
for tree, e in golden.items():
    if all(e["aarch64le"].get(n) == h for n, h in got.items()):
        sys.exit(print(f"a recorded build: agent/ as of commit {e['commit']}"))
sys.exit(f"{b} is not a recorded build (tests/regression/golden/agent_build.json): not installing it")
EOF
}
cmd=$1
case $2 in '') ;; *:*) ON_SSH_HOST=${2%%:*} ON_SSH_PORT=${2#*:} ;; *) ON_SSH_HOST=$2 ON_SSH_PORT=22 ;; esac
export ON_SSH_HOST ON_SSH_PORT
deploy() { python3 "$ON_REPO/host/tools/opticnerve_deploy.py" "$@"; }
pi() { sh "$ON_REPO/host/tools/pi_ssh.sh" "$@"; }
case $cmd in
build)
	python3 "$ON_REPO/sdp/check_sdp.py" "$QNX_SDP_DIR" || { echo "the SDP does not match the baseline: not building" >&2; exit 1; }
	bash -c '. "$QNX_SDP_DIR/qnxsdp-env.sh" > /dev/null && make -C "$ON_REPO/agent" ARCH=aarch64le' || exit 1
	sha256 "$B/opticnerve" "$B/demo_panel" "$B/launch_panel" ;;
install)
	# everything that can be checked before the running agent is stopped
	[ -n "$ON_LOGIN_PASSWORD" ] && [ -n "$ON_VNC_PASSWORD" ] || { echo "set ON_LOGIN_PASSWORD and ON_VNC_PASSWORD" >&2; exit 1; }
	for f in opticnerve demo_panel; do
		[ -f "$B/$f" ] || { echo "no $B/$f: run sut.sh build first, or set ON_AGENT_BUILD" >&2; exit 1; }
	done
	[ -z "$ON_AGENT_BUILD" ] || recorded || exit 1
	echo "on $ON_SSH_HOST, replace the agent and the demo panel (and the launcher panel, if built) with:"
	if [ -f "$B/launch_panel" ]; then sha256 "$B/opticnerve" "$B/demo_panel" "$B/launch_panel"
	else sha256 "$B/opticnerve" "$B/demo_panel"; fi
	if [ -t 0 ]; then printf 'install? [y/N] '; IFS= read -r a; [ "$a" = y ] || [ "$a" = Y ] || { echo "nothing changed"; exit 1; }; fi
	pi 'true' || { echo "cannot log in to $ON_SSH_HOST:$ON_SSH_PORT: nothing changed" >&2; exit 1; }
	sh "$ON_REPO/host/tools/demo_panel.sh" stop > /dev/null 2>&1
	deploy stop > /dev/null
	ok=1
	deploy install "$B/opticnerve" || ok=0
	if [ $ok = 1 ]; then
		SSH_ASKPASS="$ON_REPO/host/tools/pi_askpass.sh" SSH_ASKPASS_REQUIRE=force DISPLAY=x scp -q -P "$ON_SSH_PORT" -o PubkeyAuthentication=no \
			-o ConnectTimeout=10 -o StrictHostKeyChecking=accept-new "$B/demo_panel" "$ON_LOGIN_USER@$ON_SSH_HOST:demo_panel" &&
		pi 'chmod 755 demo_panel && s=$( (sha256sum demo_panel 2>/dev/null || openssl dgst -sha256 -r demo_panel) | cut -d" " -f1) &&
			[ -n "$s" ] && echo "demo panel in $HOME/demo_panel, SHA-256 $s"' || { echo "the demo panel was not installed" >&2; ok=0; }
	fi
	deploy start -logintimeout=0 || { echo "the agent did not start: it is stopped on $ON_SSH_HOST" >&2; exit 1; }
	[ $ok = 1 ] || { echo "install failed (see above); the agent was started again" >&2; exit 1; } ;;
start) deploy start -logintimeout=0 ;;
stop | status) deploy "$cmd" ;;
undo)
	sh "$ON_REPO/host/tools/demo_panel.sh" stop > /dev/null 2>&1
	deploy undo
	pi 'rm -f demo_panel demo.log && echo "demo panel removed from $HOME"' ;;
*) sed -n '5,7p' "$0" >&2; exit 2 ;;
esac
