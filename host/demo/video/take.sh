#!/bin/bash
# The demo video's shots, run in their own full-screen terminal by record_take.py. Waits on a blank screen until
# $TAKE_DIR/take_go exists, so that recording starts first, and creates $TAKE_DIR/take_done at the end.
#
# Needs: the agent installed on the target and stopped (host/tools/opticnerve_deploy.py install); demo_panel built
# (agent/build/aarch64le/demo_panel); ON_LOGIN_PASSWORD, ON_VNC_PASSWORD, ON_TIGERVNC_JAR and ON_OCULIX_JAR set, and
# optionally ON_HOST (default 10.30.1.116). Passwords come from the environment; nothing prints them. TAKE_LOGIN=1
# adds the login sanity test (host/demo/LoginTest.java), which types the login password into the target's Terminal;
# TAKE_ONLY=login records that test on its own.
cd "$(dirname "$0")/../../.." || exit 1
T=${TAKE_DIR:?TAKE_DIR not set}
H=${ON_HOST:-10.30.1.116}
say() { printf '\n\033[1;36m%s\033[0m\n' "$(printf '%s' "$*" | fold -s -w 50)"; }
run() { printf '\033[1;33m$ %s\033[0m\n' "$1"; eval "$2"; }
clear
tput civis 2>/dev/null   # no cursor on the blank screen while recording starts
while [ ! -f "$T/take_go" ]; do sleep 0.2; done
tput cnorm 2>/dev/null
if [ "$TAKE_ONLY" = login ]; then   # the login sanity test on its own (it types the login password)
    say "OpticNerve: a sanity test of a fresh QNX 8.0 quick-start image"
    sleep 5
    say "Start the OpticNerve agent on the target (Raspberry Pi 5, QNX 8.0)"
    run "opticnerve_deploy.py start" "python host/tools/opticnerve_deploy.py start"
    sleep 3
    say "Log in on the target's Terminal and check the banner, the message of the day and the shell"
    run "java LoginTest $H" "java -cp \"\$ON_TIGERVNC_JAR;\$ON_OCULIX_JAR\" host/demo/LoginTest.java $H \"\$ON_VNC_PASSWORD\" -cols=50 2>&1 | grep --line-buffered -E '^(==>|PASS|FAIL|ALL|[0-9]+ FAILED|    |  \\| )'"
    sleep 6
    say "Take the agent off the target again"
    run "opticnerve_deploy.py undo" "python host/tools/opticnerve_deploy.py undo | grep -E 'removed|device gone'"
    sleep 4
    touch "$T/take_done"
    exit 0
fi
say "OpticNerve: eyes and hands for test tools on a QNX 8.0 screen"
sleep 8
say "Start the OpticNerve agent on the target (Raspberry Pi 5, QNX 8.0)"
run "opticnerve_deploy.py start" "python host/tools/opticnerve_deploy.py start"
sleep 4
say "Start the demo panel on the target"
SSH_ASKPASS="$PWD/host/tools/pi_askpass.sh" SSH_ASKPASS_REQUIRE=force DISPLAY=x scp -q -o PubkeyAuthentication=no \
    agent/build/aarch64le/demo_panel "${ON_LOGIN_USER:-qnxuser}@$H:/data/home/qnxuser/"
run "ssh target ./demo_panel" "bash host/tools/pi_ssh.sh 'cd /data/home/qnxuser; chmod +x demo_panel; nohup ./demo_panel 600 > demo.log 2>&1 < /dev/null & sleep 1; echo demo panel running'"
sleep 4
say "Run the OculiX test against the target through the agent"
run "java DemoTest $H -slow -fail" "java -cp \"\$ON_TIGERVNC_JAR;\$ON_OCULIX_JAR\" host/demo/DemoTest.java $H \"\$ON_VNC_PASSWORD\" demo-out -slow -fail -cols=50 2>&1 | grep --line-buffered -E '^(==>|PASS|FAIL|ALL|[0-9]+ FAILED|    )'"
sleep 5
say "The same test at full speed, three times in a row (the panel without its pointer marker)"
bash host/tools/pi_ssh.sh 'cd /data/home/qnxuser; slay -f demo_panel >/dev/null 2>&1; nohup ./demo_panel -nomarker 600 > demo.log 2>&1 < /dev/null & sleep 1' > /dev/null
run "java DemoTest $H -brief -repeat=3" "java -cp \"\$ON_TIGERVNC_JAR;\$ON_OCULIX_JAR\" host/demo/DemoTest.java $H \"\$ON_VNC_PASSWORD\" demo-out -brief -repeat=3 -cols=50 2>&1 | grep --line-buffered -E '^(PASS|FAIL|ALL|[0-9]+ |run |    )'"
sleep 5
if [ "$TAKE_LOGIN" = 1 ]; then   # set by whoever runs the take: the test types the login password
    bash host/tools/pi_ssh.sh 'slay -f demo_panel >/dev/null 2>&1' > /dev/null
    say "A sanity test of the image itself: log in on the target's Terminal and check the message of the day"
    run "java LoginTest $H" "java -cp \"\$ON_TIGERVNC_JAR;\$ON_OCULIX_JAR\" host/demo/LoginTest.java $H \"\$ON_VNC_PASSWORD\" -cols=50 2>&1 | grep --line-buffered -E '^(==>|PASS|FAIL|ALL|[0-9]+ FAILED|    |  \\| )'"
    sleep 5
fi
say "Ask the agent on the target what it is doing"
run "ssh target opticnerve status" "bash host/tools/pi_ssh.sh '/data/opticnerve/bin/opticnerve status | head -9'"
sleep 6
say "Take it all off the target again"
bash host/tools/pi_ssh.sh 'cd /data/home/qnxuser; slay -f demo_panel; rm -f demo_panel demo.log' > /dev/null
run "opticnerve_deploy.py undo" "python host/tools/opticnerve_deploy.py undo | grep -E 'removed|device gone'"
sleep 4
say "Source, build instructions and tests: github.com/ davidyoung8196504567-sudo/OpticNerve"
sleep 6
rm -rf demo-out
touch "$T/take_done"
