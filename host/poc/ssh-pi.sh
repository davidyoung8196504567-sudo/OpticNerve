#!/bin/sh
# 3) An SSH session on the Raspberry Pi 5 (QNX), as qnxuser. For ON_HOST the login password comes from
# ON_LOGIN_PASSWORD; for another host, or without it, ssh asks. Root commands: sudo.
#
#   sh host/poc/ssh-pi.sh [HOST|- [COMMAND...]]      - : ON_HOST, to give a command without naming the host
POC=$(cd "$(dirname "$0")" && pwd); . "$POC/poc-env.sh"
host=${1:-$ON_HOST}
[ "$host" = - ] && host=$ON_HOST
[ $# -gt 0 ] && shift
if [ "$host" = "$ON_HOST" ] && [ -n "$ON_LOGIN_PASSWORD" ]; then
	export SSH_ASKPASS="$ON_REPO/host/tools/pi_askpass.sh" SSH_ASKPASS_REQUIRE=force
fi
ssh -o StrictHostKeyChecking=accept-new -o ConnectTimeout=10 -o PubkeyAuthentication=no "$ON_LOGIN_USER@$host" "$@"
