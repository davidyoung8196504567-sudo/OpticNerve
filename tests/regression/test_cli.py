"""The scripts' command lines: what "-", HOST:PORT and the default ports mean, and that each script refuses a wrong
parameter or a missing setting with a message, before it changes anything or reaches the network. Nothing here
leaves this host: the only connections made go to a closed port on 127.0.0.1.
"""
import sys

import pytest

from conftest import ROOT, clean_env, run

TOOLS = ROOT / "host" / "tools"
POC = ROOT / "host" / "poc"


def py(*args, env):
    return run([sys.executable, *map(str, args)], env=env)   # not -I: the tools need the user's packages


@pytest.fixture
def poc_env(tmp_path):
    """For the PoC scripts: no settings file, and logs in a scratch folder (poc-env.sh creates ON_LOGS)."""
    return clean_env(ON_CONFIG=tmp_path / "none.env", ON_LOGS=tmp_path / "logs", ON_JAVA=tmp_path / "no-java")


# host/tools/on_env.py, the rules every Python tool uses
def test_vnc_address():
    import on_env
    on_env.load()
    host, port = on_env.vnc_address("-", 5900)
    assert (host, port) == (on_env.os.environ["ON_HOST"], int(on_env.os.environ["ON_VNC_PORT"]))
    assert on_env.vnc_address("h:7", 5900) == ("h", 7)
    assert on_env.vnc_address("h", 5900) == ("h", 5900)
    assert on_env.vnc_address("h", 5901) == ("h", 5901)


@pytest.mark.parametrize("tool,args", [
    ("vnc_snapshot.py", ["-", "-", "out.png"]),
    ("vnc_input.py", ["-", "-", "wait:0"]),
    ("vnc_rate.py", ["-", "-", "1"]),
    ("vnc_latency.py", ["-", "-"]),
])
def test_dash_password_needs_the_setting(tool, args, no_settings):
    rc, out = py(TOOLS / tool, *args, env=no_settings)
    assert rc != 0 and "set ON_VNC_PASSWORD" in out, out


# host/tools/opticnerve_deploy.py
def test_deploy_usage(no_settings):
    rc, out = py(TOOLS / "opticnerve_deploy.py", env=no_settings)
    assert rc != 0 and "install" in out and "undo" in out


def test_deploy_needs_the_login_password(no_settings):
    rc, out = py(TOOLS / "opticnerve_deploy.py", "status", env=no_settings)
    assert rc != 0 and "set ON_LOGIN_PASSWORD" in out, out


@pytest.mark.parametrize("option", ["-foreground", "-passfile=/x", "-port=abc", "-logintimeout=1234567", "; reboot"])
def test_deploy_refuses_options_serve_must_not_get(option, no_settings):
    env = dict(no_settings, ON_LOGIN_PASSWORD="unused")
    rc, out = py(TOOLS / "opticnerve_deploy.py", "start", option, env=env)
    assert rc != 0 and "not a serve option" in out, out


# host/tools/pi_ssh.sh and demo_panel.sh
@pytest.fixture
def fake_ssh(tmp_path):
    """An ssh (and scp) that only prints its arguments, first on PATH: shows what a script would connect to."""
    d = tmp_path / "bin"
    d.mkdir()
    for name in ("ssh", "scp"):
        f = d / name
        f.write_text('#!/bin/sh\necho "' + name + ' $*"\n', newline="\n")
        f.chmod(0o755)
    return d


def with_path(env, d):
    return dict(env, PATH=str(d) + (";" if sys.platform == "win32" and ";" in env["PATH"] else ":") + env["PATH"])


def test_pi_ssh_connects_to_the_ssh_host_and_port(need_sh, no_settings, fake_ssh):
    env = with_path(dict(no_settings, ON_HOST="10.0.0.1", ON_SSH_HOST="jump", ON_SSH_PORT="2222",
                         ON_LOGIN_PASSWORD="unused"), fake_ssh)
    rc, out = run([need_sh, str(TOOLS / "pi_ssh.sh"), "true"], env=env)
    assert rc == 0 and "-p 2222 qnxuser@jump sh -s" in out, out


def test_pi_ssh_follows_on_host_by_default(need_sh, no_settings, fake_ssh):
    env = with_path(dict(no_settings, ON_HOST="10.0.0.1", ON_LOGIN_PASSWORD="unused"), fake_ssh)
    rc, out = run([need_sh, str(TOOLS / "pi_ssh.sh"), "true"], env=env)
    assert rc == 0 and "-p 22 qnxuser@10.0.0.1 sh -s" in out, out


@pytest.mark.parametrize("arg,want", [("10.0.0.9:2200", "-p 2200 qnxuser@10.0.0.9"), ("10.0.0.9", "-p 22 qnxuser@10.0.0.9"),
                                      ("-", "-p 2222 qnxuser@jump")])
def test_ssh_pi_host_and_port(arg, want, need_sh, poc_env, fake_ssh):
    env = with_path(dict(poc_env, ON_SSH_HOST="jump", ON_SSH_PORT="2222"), fake_ssh)
    rc, out = run([need_sh, str(POC / "ssh-pi.sh"), arg, "true"], env=env)
    assert rc == 0 and want in out, out


def test_demo_panel_needs_the_login_password(need_sh, no_settings):
    rc, out = run([need_sh, str(TOOLS / "demo_panel.sh"), "status"], env=no_settings)
    assert rc == 1 and "set ON_LOGIN_PASSWORD" in out, out


@pytest.mark.parametrize("args,status,says", [(["start", "-x"], 1, "unknown option"), (["bogus"], 2, "start")])
def test_demo_panel_refuses(args, status, says, need_sh, no_settings):
    env = dict(no_settings, ON_LOGIN_PASSWORD="unused")
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
