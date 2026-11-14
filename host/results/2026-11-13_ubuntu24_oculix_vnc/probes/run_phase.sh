#!/bin/bash
# Run one phase of PiInputProbe.java against the Pi through rfb_tap.py (ON-VNC-1 classpath: 2.0.4 jar first).
#   ON_VNC_PASSWORD=... bash run_phase.sh PHASE
# Needs: rfb_tap.py listening on 5915 and forwarding to the Pi's port 5900, writing tap.log in this folder, and a
# throwaway X display :6 for Java (OculiX's clipboard and Finder want a local display).
S=$(cd "$(dirname "$0")" && pwd)
W=$HOME/opticnerve-work
CP=$W/tigervnc-java/tigervnc-java-oculix-2.0.4.jar:$W/oculix-4.0.0/oculixapi-4.0.0-linux.jar
cd "$S"
DISPLAY=:6 LD_LIBRARY_PATH=$HOME/.cache/legerix/5.5.0-8/linux-x86-64 timeout 300 $W/jdk/jdk-25.0.4+7/bin/java \
    --enable-native-access=ALL-UNNAMED -cp $CP PiInputProbe.java 127.0.0.1 "$ON_VNC_PASSWORD" 5915 $S/tap.log $S/captures "$1" \
    > $1.stdout 2> $1.stderr < /dev/null
echo "phase $1 exit $?"
