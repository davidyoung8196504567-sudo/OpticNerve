#!/bin/sh
# Run commands on the target over SSH, without a terminal:
#
#   sh host/tools/pi_ssh.sh 'remote commands'
#
# The target is ON_SSH_HOST (default ON_HOST, 10.30.1.116), port ON_SSH_PORT (default 22), user ON_LOGIN_USER
# (default qnxuser), password ON_LOGIN_PASSWORD: from the environment or the settings file (host/tools/on_env.sh).
# Inside the remote commands, "rs CMD" runs CMD as root (sudo). The password never goes on a command line.
D=$(cd "$(dirname "$0")" && pwd)
. "$D/on_env.sh"
{ printf 'rs() { echo %s | sudo -S -p "" "$@"; }\n' "'$ON_LOGIN_PASSWORD'"; echo "$*"; } |
SSH_ASKPASS="$D/pi_askpass.sh" SSH_ASKPASS_REQUIRE=force DISPLAY=x ssh -q -o PubkeyAuthentication=no \
    -o ConnectTimeout=10 -o StrictHostKeyChecking=accept-new -p "$ON_SSH_PORT" "$ON_LOGIN_USER@$ON_SSH_HOST" sh -s
