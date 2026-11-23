"""The settings file is read by four readers (docs/SETTINGS.md): host/tools/on_env.sh, host/tools/on_env.py, cfg() in
the five Java tests and cfg() in the two OculiX IDE scripts. They must agree on every line, and on precedence (all four): the
environment, then the file, then the default. The Java and IDE readers are tested from their own source text, so the
copies in the tests are what is checked, not a re-typed version.
"""
import os
import re
import sys

import pytest

from conftest import JAVA, ROOT, clean_env, run

# Every case the grammar has to decide, and what each reader must make of it ("" = unset: the line is skipped).
SAMPLE = (
    "ON_A='q a'\n"                 # quoted, with a space
    "\tON_B=tab\n"                 # a leading tab
    "export\tON_C=x\t# c\n"        # export, a tab, a comment after a tab
    "ON_D=ab#cd\n"                 # a bare value ends at #
    'ON_E="d$HOME"\n'              # no $ expansion
    "ON_F=$(echo X)\n"             # no command: skipped
    "ON_G=1 2\n"                   # a space without quotes: skipped
    "ON_H='it's'\n"                # a quote inside quotes: skipped
    "ON_I=\n"                      # empty: unset
    "ON_J='v'\r\n"                 # a Windows line end
    "ON_K=*\n"                     # no globbing
    "ON_K='second'\n"              # the first line with a value wins
    "# ON_L='commented'\n"
)
NAMES = ["ON_A", "ON_B", "ON_C", "ON_D", "ON_E", "ON_F", "ON_G", "ON_H", "ON_I", "ON_J", "ON_K", "ON_L"]
EXPECTED = ["q a", "tab", "x", "ab", "d$HOME", "", "", "", "", "v", "*", ""]
JAVA_TESTS = ["DemoTest", "DesktopTest", "GtkDemoTest", "LoginTest", "ObstacleCourse"]
IDE_SCRIPTS = [ROOT / "host/demo/DemoTest.sikuli/DemoTest.py", ROOT / "host/ide-tests/Template.sikuli/Template.py"]


@pytest.fixture
def sample(tmp_path):
    f = tmp_path / "sample.env"
    f.write_bytes(SAMPLE.encode())
    return f


def sh_values(sh, settings, names, **env):
    script = ". host/tools/on_env.sh\n" + "".join(f'printf "%s\\n" "${{{n}-}}"\n' for n in names)
    rc, out = run([sh, "-c", script], env=clean_env(ON_CONFIG=settings, **env))
    assert rc == 0, out
    return out.split("\n")[:len(names)]


def py_values(settings, names, **env):
    code = ("import sys; sys.path.insert(0, sys.argv[1]); import on_env, os; on_env.load(); "
            "print('\\n'.join(os.environ.get(n, '') for n in sys.argv[2:]))")
    rc, out = run([sys.executable, "-I", "-c", code, str(ROOT / "host/tools"), *names],
                  env=clean_env(ON_CONFIG=settings, **env))
    assert rc == 0, out
    return out.split("\n")[:len(names)]


def java_block(name):
    """The settings code of one Java test: from its cfg() comment to vncPort()."""
    text = (ROOT / f"host/demo/{name}.java").read_text(encoding="utf-8")
    m = re.search(r"\n    // A setting \(docs/SETTINGS\.md\).*?\n    static int vncPort\(\) \{[^\n]*\n", text, re.S)
    assert m, f"{name}.java: no cfg() block"
    return m.group(0)


def ide_block(path):
    """The settings code of one OculiX IDE script: from `import os, re` to cfg()'s `return default`."""
    text = path.read_text(encoding="utf-8")
    m = re.search(r"\nimport os, re\ndef cfg\(.*?\n    return default\n", text, re.S)
    assert m, f"{path.name}: no cfg() block"
    return m.group(0)


def test_sh_reader(need_sh, sample):
    assert sh_values(need_sh, sample, NAMES) == EXPECTED


def test_python_reader(sample):
    assert py_values(sample, NAMES) == EXPECTED


@pytest.mark.slow
def test_java_reader(sample, tmp_path):
    if not JAVA:
        pytest.skip("no java on PATH")
    src = tmp_path / "Reader.java"
    src.write_text("public class Reader {" + java_block("DemoTest") + """
    public static void main(String[] a) {
        for (String n : a) System.out.println(cfg(n, ""));
    }
}
""", encoding="utf-8")
    rc, out = run([JAVA, str(src), *NAMES], env=clean_env(ON_CONFIG=sample), timeout=180)
    assert rc == 0, out
    assert out.split("\n")[:len(NAMES)] == EXPECTED
    # the environment wins over the file; an empty value in it counts as unset
    rc, out = run([JAVA, str(src), "ON_A", "ON_B"], env=clean_env(ON_CONFIG=sample, ON_A="from-env", ON_B=""),
                  timeout=180)
    assert rc == 0 and out.split("\n")[:2] == ["from-env", "tab"], out


def test_ide_reader(sample):
    # The IDE scripts are Jython 2.7; their cfg() is run here under CPython 3, which treats its file reading and
    # regular expression the same way. The two java.lang.System calls it makes are answered as Jython would.
    env = {"ON_CONFIG": str(sample)}

    class System:
        @staticmethod
        def getenv(n):
            return env.get(n)

        @staticmethod
        def getProperty(n):
            return str(sample.parent)
    space = {"System": System}
    exec(ide_block(IDE_SCRIPTS[1]), space)
    assert [space["cfg"](n) or "" for n in NAMES] == EXPECTED
    env.update(ON_A="from-env", ON_B="")   # the environment wins; an empty value in it counts as unset
    assert [space["cfg"](n) for n in ("ON_A", "ON_B")] == ["from-env", "tab"]


def test_the_five_java_tests_carry_the_same_reader():
    # The four that run SSH through pi_ssh.sh also carry pi_ssh(); the rest must be the same text in all five.
    pi_ssh = re.compile(r"    // host/tools/pi_ssh\.sh with COMMANDS.*?\n    }\n", re.S)
    blocks = {n: pi_ssh.sub("", java_block(n)) for n in JAVA_TESTS}
    assert [n for n, b in blocks.items() if b != blocks["DemoTest"]] == []


def test_the_two_ide_scripts_carry_the_same_reader():
    assert ide_block(IDE_SCRIPTS[0]) == ide_block(IDE_SCRIPTS[1])


def test_the_environment_wins_over_the_file(need_sh, sample):
    env = {"ON_A": "from-env", "ON_B": ""}   # an empty value in the environment counts as unset
    assert sh_values(need_sh, sample, ["ON_A", "ON_B"], **env) == ["from-env", "tab"]
    assert py_values(sample, ["ON_A", "ON_B"], **env) == ["from-env", "tab"]


def test_the_defaults(need_sh, no_settings, tmp_path):
    names = ["ON_HOST", "ON_VNC_PORT", "ON_SSH_HOST", "ON_SSH_PORT", "ON_LOGIN_USER"]
    want = ["10.30.1.116", "5901", "10.30.1.116", "22", "qnxuser"]
    missing = tmp_path / "none.env"
    assert sh_values(need_sh, missing, names) == want
    code = ("import sys; sys.path.insert(0, sys.argv[1]); import on_env, os; on_env.load(); "
            "print(os.environ['ON_HOST'], os.environ['ON_VNC_PORT'], on_env.ssh_host(), os.environ['ON_SSH_PORT'], "
            "os.environ['ON_LOGIN_USER'])")
    rc, out = run([sys.executable, "-I", "-c", code, str(ROOT / "host/tools")], env=no_settings)
    assert rc == 0 and out.split() == want, out


def test_ssh_host_follows_on_host_and_is_not_exported(need_sh, no_settings):
    # A script that sources the settings and starts another with a different ON_HOST: the child must send SSH to
    # its own ON_HOST, not inherit the parent's derived ON_SSH_HOST.
    script = ('. host/tools/on_env.sh; echo "exported=$(env | grep -c "^ON_SSH_HOST=")"; '
              'ON_HOST=5.5.5.5 sh -c \'. host/tools/on_env.sh; echo "child=$ON_SSH_HOST"\'')
    rc, out = run([need_sh, "-c", script], env=no_settings)
    assert rc == 0, out
    assert "exported=0" in out and "child=5.5.5.5" in out


def test_ssh_host_set_apart(need_sh, sample, tmp_path):
    f = tmp_path / "apart.env"
    f.write_text("ON_HOST='127.0.0.1'\nON_SSH_HOST='jump'\nON_SSH_PORT='2222'\n")
    assert sh_values(need_sh, f, ["ON_HOST", "ON_SSH_HOST", "ON_SSH_PORT"]) == ["127.0.0.1", "jump", "2222"]
    assert py_values(f, ["ON_HOST", "ON_SSH_HOST", "ON_SSH_PORT"]) == ["127.0.0.1", "jump", "2222"]


def test_a_bad_line_does_not_stop_a_script(need_sh, tmp_path):
    f = tmp_path / "bad.env"
    f.write_text("ON_G=1 2\nON_X=$(touch {0})\nON_HOST='9.9.9.9'\n".format(tmp_path / "ran"))
    rc, out = run([need_sh, "-c", '. host/tools/on_env.sh; echo "reached $ON_HOST"'], env=clean_env(ON_CONFIG=f))
    assert rc == 0 and "reached 9.9.9.9" in out, out
    assert not (tmp_path / "ran").exists(), "a command in the settings file was run"


def test_the_template_parses_and_holds_no_password():
    f = ROOT / "host/tools/opticnerve.env.example"
    assert b"\r" not in f.read_bytes(), "the template must stay LF (.gitattributes)"
    import on_env
    for line in f.read_text().splitlines():
        m = on_env.LINE.match(line.lstrip("#"))   # the commented-out examples too: a value typed there would ship
        if m and "PASSWORD" in m.group(1):
            assert not any(m.groups()[1:]), f"a password value in the template: {m.group(1)}"
