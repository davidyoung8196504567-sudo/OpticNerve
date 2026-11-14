#!/bin/bash
# Run InputProbe.java for the given arms (A = oculixapi alone, C = tigervnc-java-oculix 2.0.4 first)
# Needs the local VNC password in $VNC_PW (not stored here).
# against a fresh private Xtigervnc :5 (depth $DEPTH, default 16) through rfb_tap.py on port 5915.
S=$(cd "$(dirname "$0")" && pwd)
W=$HOME/opticnerve-work
O=$W/oculix-4.0.0/oculixapi-4.0.0-linux.jar
DEPTH=${DEPTH:-16}
cd "$S"
for arm in "$@"; do
    case $arm in
        A) CP=$O ;;
        C) CP=$W/tigervnc-java/tigervnc-java-oculix-2.0.4.jar:$O ;;
    esac
    pkill -x xev
    for pid in $(pgrep -x Xtigervnc); do
        tr '\0' ' ' < /proc/$pid/cmdline | grep -q ' :5 ' && kill $pid
    done
    sleep 1
    (cd $W/tigervnc && nohup usr/bin/Xtigervnc :5 -geometry 1920x1080 -depth $DEPTH -rfbport 5905 -localhost \
        -SecurityTypes VncAuth -PasswordFile $W/tigervnc/passwd > xvnc_in${DEPTH}_$arm.log 2>&1 &)
    sleep 2
    (DISPLAY=:5 nohup stdbuf -oL xev -geometry 1920x1080+0+0 > xev_${DEPTH}_$arm.log 2>&1 &)
    sleep 1
    : > tap.log
    DISPLAY=:6 LD_LIBRARY_PATH=$HOME/.cache/legerix/5.5.0-8/linux-x86-64 timeout 500 $W/jdk/jdk-25.0.4+7/bin/java \
        -verbose:class --enable-native-access=ALL-UNNAMED -cp $CP InputProbe.java 127.0.0.1 "$VNC_PW" 5915 \
        $S/xev_${DEPTH}_$arm.log $S/tap.log > raw.stdout 2> in${DEPTH}_$arm.stderr
    echo "arm $arm depth $DEPTH exit $?"
    grep -E "\] com\.(tigervnc\.rfb\.ZRLEDecoder|sikulix\.vnc\.VNCClient) " raw.stdout | sed "s#$W/##g" > in${DEPTH}_$arm.classes
    sed -E 's/\[[0-9.]+s\]\[info\]\[class,(load|init)[^]]*\][^\n]*$//' raw.stdout | grep -v '^$' > in${DEPTH}_$arm.stdout
    cp tap.log tap_${DEPTH}_$arm.log
    rm -f raw.stdout
done
