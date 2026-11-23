# Regression tests

The tests to run after changing the code. From the repository root:

```
python -m pytest                     # the host tests: no target needed (about 15 s)
python -m pytest --pi -m pi          # the target's tests (the Pi at ON_HOST, the agent running)
python -m pytest --pi                # all of them
python -m pytest -m "not slow"       # without the builds and compiles
python -m pytest -k settings         # one subject
```

The target's tests run only with `--pi`, whatever `-m` says, so no selection can reach the Pi by accident. With
`--pi`, a target that cannot be reached (no answer, no passwords) is a failure, not a skip.

**One test at a time, never in parallel.** `pytest.ini` turns pytest-xdist off (`-p no:xdist`), so `-n` is refused:
the target's tests share one agent, one demo panel and one SSH login, and several host tests start the same
scripts. Parallel runs collided in IVR_CPP-OAS.

Needs Python 3 with pytest, Pillow and cryptography; a POSIX `sh` (Git's on Windows, found and put first on PATH
also when pytest runs from PowerShell). On Windows that sh is bash, so a bash-only construct in a script passes the
parse check there; the run on Ubuntu (dash) is the one that catches it. Optional, or the tests that
need them are skipped with the reason: `java`/`javac` and an OculiX jar (`ON_OCULIX_JAR`) for the Java tests, the QNX
SDP (`QNX_SDP_DIR`, default `~/qnx805`) for the build, and for `--pi` the settings of
[docs/SETTINGS.md](../../docs/SETTINGS.md) with `ON_LOGIN_PASSWORD` and `ON_VNC_PASSWORD`.

The host tests run each script in a sandbox: a clean environment, a home folder and a settings file of their own,
and stand-in `ssh`, `scp` and `notify-send` first on `PATH` that only print what they were asked. So this host's
settings and passwords never decide a result, nothing is written to the real home, and the tests of scripts that must
refuse point every address at a closed port on 127.0.0.1: nothing they do leaves the host.

## What is here

| File | What it checks |
|---|---|
| `test_settings_readers.py` | The four readers of the settings file (`host/tools/on_env.sh`, `on_env.py`, `cfg()` in the Java tests and in the IDE scripts) give the same values for one sample full of edge cases (quotes, tabs, comments, `$`, a command, a space without quotes, CRLF, `*`, a repeated name); the environment wins over the file; the defaults; `ON_SSH_HOST` follows `ON_HOST` and is not handed down when derived; a bad line doesn't stop a script or run anything; the five Java tests and the two IDE scripts carry the same reader; the template holds no password |
| `test_cli.py` | What `-`, `HOST:PORT` and the default ports mean; where `pi_ssh.sh` and `ssh-pi.sh` connect (host and port, through the stand-in `ssh`); and that the scripts refuse wrong parameters and missing settings with a message before they change anything: the deploy tool's `start` options, `demo_panel.sh`, `sut.sh install`, `install-shortcuts.sh`, the PoC scripts without Java or with an OculiX jar that has no OpenCV for the system (a stand-in `uname` plays Linux and macOS), the VNC tools without a password; the macOS shortcuts and the prebuilt-agent check |
| `test_build.py` | Every tracked shell script parses (`sh -n`) and is LF; every Python file compiles; the Java tests compile against OculiX; the agent builds with the SDP (in a scratch copy, `-Werror`) and its bytes equal the ones recorded for that state of `agent/` in `golden/agent_build.json` |
| `test_target.py` (`--pi`) | SSH; the agent's status; a frame from the agent at the settings' address, the size the agent reports; SSH through a port forward on another port (SSH apart from VNC); `core_test` built here and run on the target; `host/tests/robustness.py`; the demo panel starting and stopping |
| `record_build.py` | Records the binaries' SHA-256 for the committed `agent/` in `golden/agent_build.json`, after checking the SDP against the baseline. Run it after a reviewed agent change is committed, never to make a failing comparison pass: a changed hash with an unchanged `agent/` is a finding |

## The target's tests

They change nothing a person could be using: none moves the pointer or types. `test_robustness` fills every client
slot and the demo panel test shows the panel on the target's screen for a few seconds, so both are skipped while any
client is connected to the agent (also one still at its password prompt); someone connecting during the minute or
two the robustness checks take is refused. The demo panel test is also skipped while a panel is running.
`test_core_test_on_the_target` works in a folder of its own (`ontest-<random>` in the login user's home) and removes
it; the port forward test takes the forward's one-off entry out of `~/.ssh/known_hosts` again.

After a change to `agent/` is committed, the byte comparison is skipped (the summary says so) until
`record_build.py` records the new state; the file names the commit that last changed `agent/`. When the bytes
differ, the test runs `sdp/check_sdp.py` and says whether the SDP still matches the baseline, which tells a changed
SDP from a changed build. `record_build.py` refuses to record over an entry with other bytes.

Tests that drive the screen (`host/demo/DemoTest.java` and the others, the IDE tests) are not here: they move the
target's pointer, so they are run one at a time, watched, when agreed.
