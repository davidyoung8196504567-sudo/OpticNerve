#!/bin/sh
# 1) Start the OculiX IDE, with the target's address and passwords in its environment, so tests can read them and
# start the demo panel. It opens as a tray icon: open a workspace from it. Log: ~/ide-session/ide.log.
#
#   sh host/poc/ide.sh
POC=$(cd "$(dirname "$0")" && pwd); . "$POC/poc-env.sh"
need_java
if oculix_running | grep -q ' -r '; then say "a test is running (run-test.sh): start the IDE when it has finished"; exit 1; fi
if oculix_running > /dev/null; then say "the IDE is already running (its tray icon)"; exit 0; fi
cd "$ON_TESTS" 2>/dev/null || cd "$HOME"
detach "$ON_JAVA" -cp "$ON_OCULIX_JAR" org.sikuli.ide.Sikulix >> "$ON_LOGS/ide.log" 2>&1 < /dev/null &
say "IDE starting: look for its tray icon, then open a workspace (log $ON_LOGS/ide.log)"
