#!/bin/sh
# SSH_ASKPASS helper for pi_ssh.sh: prints the target login password from the environment (never stored in the repo).
echo "$ON_LOGIN_PASSWORD"
