# Sourced by the PoC commands in this folder (each sets POC to this folder first). The settings come from the
# command line, then the environment, then the settings file ~/.config/opticnerve/env (mode 600, never in a repo;
# template host/tools/opticnerve.env.example), then the defaults: host/tools/on_env.sh for the target's addresses,
# ports and passwords, below for this tester's Java, OculiX jar and folders.
#
# POC_HOLD=1 (set by the desktop shortcuts) waits for Enter before the terminal window closes.
ON_REPO=$(cd "$POC/../.." && pwd)
. "$ON_REPO/host/tools/on_env.sh"
: "${ON_JAVA:=$HOME/opticnerve-work/jdk/jdk17/bin/java}"
: "${ON_OCULIX_JAR:=$HOME/qtb1/oculixide-5.0.0-complete-lux.jar}"
: "${ON_TESTS:=$HOME/ide-tests}" "${ON_LOGS:=$HOME/ide-session}" "${QNX_SDP_DIR:=$HOME/qnx805}"
export ON_REPO ON_JAVA ON_OCULIX_JAR ON_TESTS ON_LOGS QNX_SDP_DIR
mkdir -p "$ON_LOGS"

# From SSH or cron there is no DISPLAY: use the desktop this user is logged in to (the X socket it owns)
if [ -z "$DISPLAY" ]; then
	for x in /tmp/.X11-unix/X*; do [ -O "$x" ] && DISPLAY=:${x#/tmp/.X11-unix/X} && export DISPLAY && break; done
fi
[ -n "$XAUTHORITY" ] || [ ! -r "/run/user/$(id -u)/gdm/Xauthority" ] || export XAUTHORITY="/run/user/$(id -u)/gdm/Xauthority"

[ "$POC_HOLD" = 1 ] && trap 'stty echo 2>/dev/null; printf "\nPress Enter to close. "; read _' EXIT
poc_envf=$HOME/.config/opticnerve/env
[ ! -e "$poc_envf" ] || [ -z "$(find "$poc_envf" -perm /077)" ] || echo "warning: $poc_envf is readable by others: chmod 600 it" >&2

# A message, also as a desktop notification when there is no terminal (a shortcut without one)
say() { echo "$*"; [ -t 1 ] || ! command -v notify-send > /dev/null || notify-send "OpticNerve PoC" "$*"; }
# Running OculiX processes (the IDE, or a test run with -r). Two at once on one desktop crash each other in OculiX's
# hotkey listener (JXGrabKey, a SIGSEGV within seconds), so the IDE and run-test.sh refuse to start beside another.
oculix_running() { pgrep -af '[o]rg.sikuli.ide.Sikulix'; }
need_java() {
	[ -x "$ON_JAVA" ] || { say "no Java at $ON_JAVA: set ON_JAVA in ~/.config/opticnerve/env" >&2; exit 1; }
	[ -r "$ON_OCULIX_JAR" ] || { say "no OculiX jar at $ON_OCULIX_JAR: set ON_OCULIX_JAR" >&2; exit 1; }
}
