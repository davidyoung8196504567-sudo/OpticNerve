#!/bin/sh
# 2) Open OculiX's embedded VNC viewer (TigerVNC Java) on a VNC server, with control: mouse and keyboard go to the
# target (never view-only). Log: ~/ide-session/viewer-<host>-<port>.log.
#
#   sh host/poc/viewer.sh [HOST[::PORT]]
#
# HOST defaults to ON_HOST and PORT to 5901 (the OpticNerve agent; 5900 is the usual port of other VNC servers).
# The password: for the agent at ON_HOST::5901, ON_VNC_PASSWORD; for any other server, VNC_PASSWORD, or asked in the
# terminal. Run in a terminal without HOST, it asks for the address and the password (Enter keeps the saved one).
# The password goes to the viewer in a file only this user can read, deleted once the viewer has logged in.
POC=$(cd "$(dirname "$0")" && pwd); . "$POC/poc-env.sh"
need_java
pwfile=
trap 'stty echo 2>/dev/null; rm -f "$pwfile"; exit 130' INT TERM HUP
target=$1
if [ -z "$target" ] && [ -t 0 ]; then
	printf 'VNC server [%s::5901]: ' "$ON_HOST"; IFS= read -r target
fi
target=${target:-$ON_HOST}
case $target in *:*) ;; *) target=$target::5901 ;; esac   # HOST::PORT or HOST:DISPLAY as given
if [ "$target" = "$ON_HOST::5901" ] && [ -n "$ON_VNC_PASSWORD" ]; then pw=$ON_VNC_PASSWORD; else pw=$VNC_PASSWORD; fi
if [ -t 0 ] && { [ -z "$1" ] || [ -z "$pw" ]; }; then
	[ -n "$pw" ] && printf 'VNC password (Enter: the saved one): ' || printf 'VNC password: '
	stty -echo; IFS= read -r typed; stty echo; echo
	[ -n "$typed" ] && pw=$typed
fi

if [ -n "$pw" ]; then   # the VncAuth password file format: the password DES-encrypted with the fixed VNC key
	dir=${XDG_RUNTIME_DIR:-$ON_LOGS}   # /run/user/UID: memory only, this user only, cleared at logout
	pwfile=$(mktemp "$dir/vncpasswd.XXXXXX") || exit 1
	PW=$pw python3 -I -W ignore -c '
import os, sys
try:
    from cryptography.hazmat.decrepit.ciphers.algorithms import TripleDES
except ImportError:
    from cryptography.hazmat.primitives.ciphers.algorithms import TripleDES
from cryptography.hazmat.primitives.ciphers import Cipher, modes
pw = os.environ["PW"].encode()[:8].ljust(8, b"\0")
e = Cipher(TripleDES(bytes([0xe8, 0x4a, 0xd6, 0x60, 0xc4, 0x72, 0x1a, 0xe0]) * 3), modes.ECB()).encryptor()
open(sys.argv[1], "wb").write(e.update(pw) + e.finalize())
' "$pwfile" || { rm -f "$pwfile"; exit 1; }
fi
log=$ON_LOGS/viewer-$(printf '%s' "$target" | tr -c 'A-Za-z0-9.-' '-').log
setsid nohup "$ON_JAVA" -cp "$ON_OCULIX_JAR" com.tigervnc.vncviewer.VncViewer -AutoSelect=0 -FullColor=1 \
	${pwfile:+-PasswordFile=$pwfile} "$target" > "$log" 2>&1 < /dev/null &
vpid=$!
echo "viewer starting on $target"
i=0   # wait for the login: the first frame's pixel format, the viewer ending, or 30 s
until grep -q 'pixel format' "$log" || ! kill -0 $vpid 2>/dev/null || [ $i -ge 30 ]; do sleep 1; i=$((i + 1)); done
rm -f "$pwfile"
if grep -q 'pixel format' "$log"; then echo "connected to $target"
elif kill -0 $vpid 2>/dev/null; then echo "not connected after 30 s; the viewer is still open. Its log ($log):"; tail -3 "$log"
else echo "the viewer ended without connecting. Its log ($log):"; tail -3 "$log"; exit 1; fi
