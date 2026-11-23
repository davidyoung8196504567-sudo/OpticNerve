"""The OpticNerve settings for the Python tools, as host/tools/on_env.sh gives them to the shell scripts: each value
comes from the first of the command line (the tool applies it after load()), the environment, the settings file
ON_CONFIG (an absolute path; default ~/.config/opticnerve/env; template host/tools/opticnerve.env.example), the
defaults below. The file's grammar is on_env.sh's: [export] NAME=VALUE [# comment], VALUE 'text', "text" or text
without spaces, # or quotes, taken literally; other lines are skipped.

    sys.path.insert(0, str(<repo>/"host"/"tools")); import on_env; on_env.load()
"""
import os, re
from pathlib import Path

DEFAULTS = {"ON_HOST": "10.30.1.116", "ON_VNC_PORT": "5901", "ON_SSH_PORT": "22", "ON_LOGIN_USER": "qnxuser"}
LINE = re.compile(r"""^\s*(?:export\s+)?(ON_[A-Z0-9_]+)=(?:'([^']*)'|"([^"]*)"|([^\s#'"]*))\s*(?:#.*)?$""")


def load():
    f = Path(os.environ.get("ON_CONFIG") or Path.home() / ".config" / "opticnerve" / "env")
    if f.is_file():
        for line in f.read_text(encoding="utf-8", errors="replace").splitlines():
            m = LINE.match(line)
            if not m:
                continue
            value = next((g for g in m.groups()[1:] if g is not None), "")
            if value and not os.environ.get(m.group(1)):   # a value in the environment wins over the file
                os.environ[m.group(1)] = value
    for name, value in DEFAULTS.items():
        if not os.environ.get(name):
            os.environ[name] = value


def ssh_host():
    """ON_SSH_HOST, or the VNC address ON_HOST when SSH is not set apart (not written to the environment, so that a
    program started from here with another ON_HOST derives its own)."""
    return os.environ.get("ON_SSH_HOST") or os.environ["ON_HOST"]


def vnc_address(arg, default_port):
    """HOST[:PORT] from the command line, or "-" for the agent from the settings (ON_HOST, ON_VNC_PORT)."""
    if arg == "-":
        return os.environ["ON_HOST"], int(os.environ["ON_VNC_PORT"])
    host, _, port = arg.partition(":")
    return host, int(port or default_port)


def vnc_password(arg):
    """The password from the command line, or "-" for ON_VNC_PASSWORD (which keeps it out of the process list)."""
    if arg != "-":
        return arg
    if not os.environ.get("ON_VNC_PASSWORD"):
        raise SystemExit("set ON_VNC_PASSWORD (in the environment or the settings file)")
    return os.environ["ON_VNC_PASSWORD"]
