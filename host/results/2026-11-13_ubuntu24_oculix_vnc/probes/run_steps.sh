#!/bin/bash
# Run one StepProbe script against the Pi through rfb_tap.py (ON-VNC-1: 2.0.4 jar first), with a wall-clock limit.
#   ON_VNC_PASSWORD=... bash run_steps.sh LABEL      (reads LABEL.steps, writes LABEL.stdout, LABEL.stderr, captures/)
# Needs rfb_tap.py on port 5915 forwarding to the Pi's 5900 and writing tap.log here, and a throwaway X display :6.
S=$(cd "$(dirname "$0")" && pwd)
W=$HOME/opticnerve-work
CP=$W/tigervnc-java/tigervnc-java-oculix-2.0.4.jar:$W/oculix-4.0.0/oculixapi-4.0.0-linux.jar
cd "$S"
DISPLAY=:6 timeout ${LIMIT:-180} $W/jdk/jdk-25.0.4+7/bin/java --enable-native-access=ALL-UNNAMED -cp $CP StepProbe.java \
    127.0.0.1 "$ON_VNC_PASSWORD" 5915 $S/tap.log $S/captures "$1" $S/$1.steps 2> >(head -c 200000 > $1.stderr) > $1.stdout < /dev/null
echo "$1 exit $?"
grep -vE "DEBUG|^\[|^os |^started" $1.stdout | cut -c1-${COLS:-230}
grep -E "Exception" $1.stderr | head -3
