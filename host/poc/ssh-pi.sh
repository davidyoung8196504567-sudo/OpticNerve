#!/bin/sh
# 3) An SSH session on the Raspberry Pi 5 (QNX), as ON_LOGIN_USER (qnxuser). Root commands: sudo.
#
#   sh host/poc/ssh-pi.sh [HOST[:PORT]|- [COMMAND...]]      - : the saved target, to give a command without naming it
#
# HOST and PORT default to ON_SSH_HOST (default ON_HOST) and ON_SSH_PORT (default 22), from the environment or the
# settings file; a HOST given without :PORT means port 22. For the saved target the login password comes from ON_LOGIN_PASSWORD; otherwise ssh asks.
POC=$(cd "$(dirname "$0")" && pwd); . "$POC/poc-env.sh"
host=$ON_SSH_HOST port=$ON_SSH_PORT
if [ -n "$1" ] && [ "$1" != - ]; then
	case $1 in *:*) host=${1%%:*} port=${1#*:} ;; *) host=$1 port=22 ;; esac
fi
[ $# -gt 0 ] && shift
if [ "$host" = "$ON_SSH_HOST" ] && [ -n "$ON_LOGIN_PASSWORD" ]; then
	export SSH_ASKPASS="$ON_REPO/host/tools/pi_askpass.sh" SSH_ASKPASS_REQUIRE=force
fi
ssh -o StrictHostKeyChecking=accept-new -o ConnectTimeout=10 -o PubkeyAuthentication=no -p "$port" \
	"$ON_LOGIN_USER@$host" "$@"
