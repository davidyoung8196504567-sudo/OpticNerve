#!/bin/sh
# 5) Run a recorded OculiX test without the IDE, and say PASS or FAIL. It drives the target like a run in the IDE
# (it needs this desktop for OculiX, and moves the target's pointer). Log: ~/ide-session/run-<test>-<UTC>.log.
# It refuses while the IDE is open: two OculiX processes on one desktop crash each other (see poc-env.sh).
#
#   sh host/poc/run-test.sh [TEST.sikuli]
#
# A name without a folder is looked for in ~/ide-tests. Run in a terminal without a test, it lists them to choose.
POC=$(cd "$(dirname "$0")" && pwd); . "$POC/poc-env.sh"
need_java
need_opencv
if oculix_running > /dev/null; then
	echo "OculiX is running (the IDE, or another test): close it first; two at once crash each other" >&2; exit 1
fi
t=$1
if [ -z "$t" ] && [ -t 0 ]; then
	i=0
	for f in "$ON_TESTS"/*.sikuli; do [ -d "$f" ] && i=$((i + 1)) && echo "  $i) $(basename "$f")"; done
	[ $i -gt 0 ] || { echo "no tests in $ON_TESTS"; exit 1; }
	printf 'test number: '; IFS= read -r n
	i=0
	for f in "$ON_TESTS"/*.sikuli; do [ -d "$f" ] && i=$((i + 1)) && [ "$i" = "$n" ] && t=$f; done
fi
[ -n "$t" ] || { sed -n '5p' "$0" >&2; exit 2; }
case $t in */*) ;; *) t=$ON_TESTS/$t ;; esac
case $t in *.sikuli) ;; *) t=$t.sikuli ;; esac
[ -d "$t" ] || { echo "no test at $t" >&2; exit 1; }
name=$(basename "$t" .sikuli)
log=$ON_LOGS/run-$name-$(date -u +%Y%m%dT%H%M%SZ).log
echo "running $name against $ON_HOST (log $log)"
"$ON_JAVA" -cp "$ON_OCULIX_JAR" org.sikuli.ide.Sikulix -r "$t" > "$log" 2>&1
rc=$?
grep -viE '^\[(debug|info)' "$log" | tail -20
# FAIL: a non-zero exit (an error, or exit(n)), or the "N FAILED: ..." line of tests made from Template.sikuli
if [ $rc -eq 0 ] && ! grep -qE '^[0-9]+ FAILED' "$log"; then echo "PASS: $name"; exit 0; fi
echo "FAIL: $name (exit $rc)"
exit 1
