#!/bin/bash
# Run SubRegionProbe.java: bash run_subregion.sh DEPTH ARM...   (A = oculixapi alone, C = 2.0.4 jar first)
# Needs the local VNC password in $VNC_PW (the password of the private Xtigervnc display, not stored here).
S=$(cd "$(dirname "$0")" && pwd)
W=$HOME/opticnerve-work
O=$W/oculix-4.0.0/oculixapi-4.0.0-linux.jar
DEPTH=$1
shift
cd "$S"
for arm in "$@"; do
    case $arm in
        A) CP=$O ;;
        C) CP=$W/tigervnc-java/tigervnc-java-oculix-2.0.4.jar:$O ;;
    esac
    pkill -x Xtigervnc
    sleep 1
    (cd $W/tigervnc && nohup usr/bin/Xtigervnc :5 -geometry 1920x1080 -depth $DEPTH -rfbport 5905 -localhost \
        -SecurityTypes VncAuth -PasswordFile $W/tigervnc/passwd > xvnc_sub${DEPTH}_$arm.log 2>&1 < /dev/null &)
    sleep 2
    timeout 300 $W/jdk/jdk-25.0.4+7/bin/java --enable-native-access=ALL-UNNAMED -cp $CP SubRegionProbe.java \
        127.0.0.1 "$VNC_PW" 5905 :5 > sub${DEPTH}_$arm.stdout 2> sub${DEPTH}_$arm.stderr < /dev/null
    echo "arm $arm depth $DEPTH exit $?"
    grep -E "FramebufferUpdateRequest" $W/tigervnc/xvnc_sub${DEPTH}_$arm.log | sort | uniq -c > sub${DEPTH}_$arm.serverlog
done
pkill -x Xtigervnc
