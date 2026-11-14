#!/bin/sh
# Run commands on the target over SSH, without a terminal:
#
#   ON_LOGIN_PASSWORD=... [ON_HOST=10.30.1.116] [ON_LOGIN_USER=qnxuser] sh host/tools/pi_ssh.sh 'remote commands'
#
# Inside the remote commands, "rs CMD" runs CMD as root (sudo). The password comes from the environment only.
D=$(cd "$(dirname "$0")" && pwd)
{ printf 'rs() { echo %s | sudo -S -p "" "$@"; }\n' "'$ON_LOGIN_PASSWORD'"; echo "$*"; } |
SSH_ASKPASS="$D/pi_askpass.sh" SSH_ASKPASS_REQUIRE=force DISPLAY=x ssh -q -o PubkeyAuthentication=no \
    -o ConnectTimeout=10 -o StrictHostKeyChecking=accept-new "${ON_LOGIN_USER:-qnxuser}@${ON_HOST:-10.30.1.116}" sh -s
