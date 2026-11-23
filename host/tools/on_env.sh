# Sourced by the shell scripts: the OpticNerve settings (addresses, ports, user, passwords). Each value comes from
# the first of: the script's command line (the script applies it after sourcing this), the environment, the settings
# file ON_CONFIG (an absolute path; default ~/.config/opticnerve/env; template host/tools/opticnerve.env.example),
# the defaults below. docs/SETTINGS.md lists them.
#
# The file's grammar, the same in every reader (on_env.py, the Java tests' cfg(), the IDE scripts' cfg()): a line is
# [export] NAME=VALUE [# comment], NAME starting with ON_; VALUE is 'text', "text" or text without spaces, # or
# quotes, taken literally (no $ expansion). Other lines are skipped.
on_cfg=${ON_CONFIG:-$HOME/.config/opticnerve/env}
if [ -r "$on_cfg" ]; then
	# NAME=VALUE pairs, one per line, from the lines that follow the grammar; carriage returns dropped
	on_pairs=$(tr -d '\r' < "$on_cfg" | sed -n -E \
		"s/^[[:space:]]*(export[[:space:]]+)?(ON_[A-Z0-9_]+)=('([^']*)'|\"([^\"]*)\"|([^[:space:]#'\"]*))[[:space:]]*(#.*)?$/\2=\4\5\6/p")
	on_nl='
'
	on_ifs=$IFS IFS=$on_nl on_glob=1
	case $- in *f*) on_glob= ;; esac
	set -f   # a value may hold * or ?
	for on_pair in $on_pairs; do
		on_name=${on_pair%%=*}
		eval "on_have=\${$on_name:-}"   # on_name is ON_[A-Z0-9_]+, so this names a variable and nothing else
		[ -n "$on_have" ] || [ -z "${on_pair#*=}" ] || export "$on_pair"   # the environment wins over the file
	done
	IFS=$on_ifs
	[ -z "$on_glob" ] || set +f
	unset on_pairs on_nl on_ifs on_glob on_pair on_name on_have
fi
unset on_cfg
: "${ON_HOST:=10.30.1.116}" "${ON_VNC_PORT:=5901}" "${ON_SSH_PORT:=22}" "${ON_LOGIN_USER:=qnxuser}"
export ON_HOST ON_VNC_PORT ON_SSH_PORT ON_LOGIN_USER
# SSH goes to the VNC address unless set apart (a port forward, a tunnel). Not exported when derived here, so that a
# program started from this script with another ON_HOST derives its own.
: "${ON_SSH_HOST:=$ON_HOST}"
