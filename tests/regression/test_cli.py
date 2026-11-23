"""The scripts' command lines: what "-", HOST:PORT and the default ports mean, and that each script refuses a wrong
parameter or a missing setting with a message, before it changes anything or reaches the network. Nothing here
leaves this host: the scripts run in the sandbox of conftest.py, whose stand-in ssh only prints where it would
connect, and the refusal tests point every address at a closed port on 127.0.0.1 (safe_target).
"""
import os
import sys

import pytest

from conftest import ROOT, run

TOOLS = ROOT / "host" / "tools"
POC = ROOT / "host" / "poc"


def py(*args, env):
    return run([sys.executable, *map(str, args)], env=env)   # not -I: the tools need the user's packages


@pytest.fixture
def poc_env(safe_target, tmp_path):
    """For the PoC scripts: the safe target, and logs in a scratch folder (poc-env.sh creates ON_LOGS)."""
    return dict(safe_target, ON_LOGS=str(tmp_path / "logs"), ON_JAVA=str(tmp_path / "no-java"))


# host/tools/on_env.py, the rules every Python tool uses
def test_vnc_address(monkeypatch, tmp_path):
    import os
    import on_env
    # an environment of its own, so the real settings are neither read nor left behind in this process
    monkeypatch.setattr(os, "environ", {"ON_CONFIG": str(tmp_path / "x.env")})
    (tmp_path / "x.env").write_text("ON_HOST='10.9.8.7'\nON_VNC_PORT='5999'\n")
    on_env.load()
    assert on_env.vnc_address("-", 5900) == ("10.9.8.7", 5999)
    assert on_env.vnc_address("h:7", 5900) == ("h", 7)
    assert on_env.vnc_address("h", 5900) == ("h", 5900)
    assert on_env.vnc_address("h", 5901) == ("h", 5901)


@pytest.mark.parametrize("tool,args", [
    ("vnc_snapshot.py", ["-", "-", "out.png"]),
    ("vnc_input.py", ["-", "-", "wait:0"]),
    ("vnc_rate.py", ["-", "-", "1"]),
    ("vnc_latency.py", ["-", "-"]),
])
def test_dash_password_needs_the_setting(tool, args, safe_target):
    rc, out = py(TOOLS / tool, *args, env=safe_target)
    assert rc != 0 and "set ON_VNC_PASSWORD" in out, out


# host/tools/opticnerve_deploy.py
def test_deploy_usage(safe_target):
    rc, out = py(TOOLS / "opticnerve_deploy.py", env=safe_target)
    assert rc != 0 and "install" in out and "undo" in out


def test_deploy_needs_the_login_password(safe_target):
    rc, out = py(TOOLS / "opticnerve_deploy.py", "status", env=safe_target)
    assert rc != 0 and "set ON_LOGIN_PASSWORD" in out, out


@pytest.mark.parametrize("option", ["-foreground", "-passfile=/x", "-port=abc", "-logintimeout=1234567", "; reboot"])
def test_deploy_refuses_options_serve_must_not_get(option, safe_target):
    env = dict(safe_target, ON_LOGIN_PASSWORD="unused")
    rc, out = py(TOOLS / "opticnerve_deploy.py", "start", option, env=env)
    assert rc != 0 and "not a serve option" in out, out


# host/tools/pi_ssh.sh and demo_panel.sh
# Where the scripts would connect: the sandbox's stand-in ssh prints its arguments
def test_pi_ssh_connects_to_the_ssh_host_and_port(need_sh, no_settings):
    env = dict(no_settings, ON_HOST="10.0.0.1", ON_SSH_HOST="jump", ON_SSH_PORT="2222", ON_LOGIN_PASSWORD="unused")
    rc, out = run([need_sh, str(TOOLS / "pi_ssh.sh"), "true"], env=env)
    assert rc == 0 and "-p 2222 qnxuser@jump sh -s" in out, out


def test_pi_ssh_follows_on_host_by_default(need_sh, no_settings):
    env = dict(no_settings, ON_HOST="10.0.0.1", ON_LOGIN_PASSWORD="unused")
    rc, out = run([need_sh, str(TOOLS / "pi_ssh.sh"), "true"], env=env)
    assert rc == 0 and "-p 22 qnxuser@10.0.0.1 sh -s" in out, out


@pytest.mark.parametrize("arg,want", [("10.0.0.9:2200", "-p 2200 qnxuser@10.0.0.9"), ("10.0.0.9", "-p 22 qnxuser@10.0.0.9"),
                                      ("-", "-p 2222 qnxuser@jump")])
def test_ssh_pi_host_and_port(arg, want, need_sh, poc_env):
    env = dict(poc_env, ON_SSH_HOST="jump", ON_SSH_PORT="2222")
    rc, out = run([need_sh, str(POC / "ssh-pi.sh"), arg, "true"], env=env)
    assert rc == 0 and want in out, out


def test_demo_panel_needs_the_login_password(need_sh, safe_target):
    rc, out = run([need_sh, str(TOOLS / "demo_panel.sh"), "status"], env=safe_target)
    assert rc == 1 and "set ON_LOGIN_PASSWORD" in out, out


@pytest.mark.parametrize("args,status,says", [(["start", "-x"], 1, "unknown option"), (["bogus"], 2, "start")])
def test_demo_panel_refuses(args, status, says, need_sh, safe_target):
    env = dict(safe_target, ON_LOGIN_PASSWORD="unused")
    rc, out = run([need_sh, str(TOOLS / "demo_panel.sh"), *args], env=env)
    assert rc == status and says in out, out


# host/poc
def test_sut_usage(need_sh, poc_env):
    rc, out = run([need_sh, str(POC / "sut.sh"), "bogus"], env=poc_env)
    assert rc == 2 and "sut.sh build" in out, out


def test_sut_install_checks_before_it_stops_anything(need_sh, poc_env):
    rc, out = run([need_sh, str(POC / "sut.sh"), "install"], env=poc_env)
    assert rc == 1 and "set ON_LOGIN_PASSWORD and ON_VNC_PASSWORD" in out, out


def test_install_shortcuts_refuses_an_unknown_option(need_sh, poc_env):
    rc, out = run([need_sh, str(POC / "install-shortcuts.sh"), "--remove"], env=poc_env)
    assert rc == 2 and "-remove" in out, out


@pytest.mark.parametrize("script", ["ide.sh", "run-test.sh", "viewer.sh"])
def test_poc_scripts_say_when_java_is_missing(script, need_sh, poc_env):
    rc, out = run([need_sh, str(POC / script)], env=poc_env, input="")
    assert rc == 1 and "no Java at" in out, out


def test_sut_refuses_a_prebuilt_agent_that_was_not_recorded(need_sh, poc_env, tmp_path):
    built = tmp_path / "prebuilt"
    built.mkdir()
    for f in ("opticnerve", "demo_panel"):
        (built / f).write_bytes(b"not the agent")
    env = dict(poc_env, ON_AGENT_BUILD=built, ON_LOGIN_PASSWORD="unused", ON_VNC_PASSWORD="unused")
    rc, out = run([need_sh, str(POC / "sut.sh"), "install"], env={k: str(v) for k, v in env.items()}, input="")
    assert rc == 1 and "not a recorded build" in out, out
    assert "ssh " not in out, "it went on to the target"


@pytest.mark.parametrize("system", ["Linux", "Darwin"])
def test_install_shortcuts(system, need_sh, poc_env, tmp_path):
    """In the sandbox's home: Linux launchers, or macOS .command files (a stand-in uname says which system)."""
    bin_ = tmp_path / "uname-bin"
    bin_.mkdir()
    (bin_ / "uname").write_text(f'#!/bin/sh\necho {system}\n', newline="\n")
    (bin_ / "uname").chmod(0o755)
    home = tmp_path / "home"
    env = dict(poc_env, HOME=str(home), USERPROFILE=str(home), PATH=str(bin_) + os.pathsep + poc_env["PATH"])
    rc, out = run([need_sh, str(POC / "install-shortcuts.sh")], env=env)
    assert rc == 0 and out.count("shortcut: ") == 6, out
    made = sorted(p.name for p in (home / "Desktop").iterdir())
    if system == "Darwin":
        assert "OculiX VNC viewer.command" in made and len(made) == 6, made
        text = (home / "Desktop" / "OculiX VNC viewer.command").read_text()
        assert "POC_HOLD=1 exec sh " in text and text.rstrip().endswith("host/poc/viewer.sh"), text
    else:
        assert "opticnerve-oculix-viewer.desktop" in made and len(made) == 6, made
        assert len(list((home / ".local/share/applications").iterdir())) == 6
    rc, out = run([need_sh, str(POC / "install-shortcuts.sh"), "-remove"], env=env)
    assert rc == 0 and list((home / "Desktop").iterdir()) == [], out
