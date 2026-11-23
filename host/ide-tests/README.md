# OculiX IDE tests

Tests written in the OculiX IDE, one `.sikuli` folder each (the script and its captured images). They drive the Pi's
screen through the OpticNerve agent on port 5901. `host/demo/DemoTest.sikuli` is a finished example.

## Start the IDE

Set `ON_HOST` (the Pi's address) and `ON_VNC_PASSWORD` (the agent's VNC password), and `ON_VNC_PORT` if it isn't
5901, in the shell or in the settings file `~/.config/opticnerve/env` ([docs/SETTINGS.md](../../docs/SETTINGS.md)),
then:

```
java -cp oculixide-5.0.0-complete-win.jar org.sikuli.ide.Sikulix
```

That is the OculiX development build we test with (chore/global-bug-fixes 870a7ff4, not a release), which has
tigervnc-java-oculix 2.0.4 inside. With the OculiX 4.0.0 release, put the TigerVNC jar first:
`java -cp "tigervnc-java-oculix-2.0.4.jar;oculixide-4.0.0-windows.jar" org.sikuli.ide.Sikulix`.

To work the Pi's apps and capture images, run a VNC viewer full screen on a monitor at 100 % scale, **with
control** (never view-only: a tester has to interact with what they test). The viewer and the test take turns: the
agent injects the input of the client that sends it, unless the other one is holding a key or a button. Keep your
hands off the viewer while a test runs, or your mouse moves the Pi's pointer under the test. We use TigerVNC 1.16.2:
`vncviewer64-1.16.2.exe AlwaysCursor=1 CursorType=System FullColor=1 AutoSelect=0 <Pi address>::5901`.
`AlwaysCursor=1 CursorType=System` keeps this computer's pointer visible in the viewer (where you aim).

The agent draws its own arrow into a viewer's picture where the Pi's pointer is (ON-AG-001 in
[docs/ISSUES.md](../../docs/ISSUES.md)): at the last place the agent moved it, for you or for a test. It follows moves
made through the agent, not moves of a mouse plugged into the Pi. Test connections never get the arrow, but **the
images you capture from a viewer do**: switch it off before capturing, `python host/tools/opticnerve_deploy.py
pointer off` (and `pointer on` afterwards). With it off, images captured from the viewer match the Pi exactly.

## Write a test

Start from `Template.sikuli`: copy the folder, rename the folder and its `.py` to the test's name.

- **Input by hand, searches on `vnc`**: click with `click_on(image)` (or `hand_click(x, y)`) and type with
  `hand_type("text")`, the template's hand: the pointer glides to the target and clicks as a person does, and keys
  come one at a time (the same model as every other test, `host/tools/hand.py`). Search with `vnc.exists(...)`,
  `vnc.wait(...)`. A bare `click("x.png")` searches and clicks this computer's screen, at machine speed.
- **One capture per line, named straight away.** The IDE names a capture after the first word of a line that has an
  `=` (`plus = <capture>` gives `plus.png`), and it overwrites an existing file of that name. Several captures on one
  line, or two lines starting with the same name, overwrite each other (reported to OculiX; still in 870a7ff4).
- **Measure positions from something you found**, not from the screen's corner.
- **Leave the screen as you found it.** End with the steps that undo the test (close what it opened, reset what it
  changed), and check the start state again, so runs can follow one another.
- **No passwords in scripts**: read them from the environment (`System.getenv`).
- Enter is `hand_type("\r")`.
- **Start applications over SSH**, not from the quick-start launcher's tiles, so a test doesn't depend on what ran
  before it. For the demo panel, `host/tools/demo_panel.sh start` stops any panel and starts a fresh one (count 0,
  lamp GREEN, no pointer marker; `-marker` to show it), and `stop` ends it. It needs `ON_LOGIN_PASSWORD` (the Pi's
  login) and `ON_REPO` (this repository's folder) in the environment the IDE was started from. In a test, before
  connecting:

  ```
  import subprocess
  def demo_panel(cmd):
      rc = subprocess.call(["sh", System.getenv("ON_REPO") + "/host/tools/demo_panel.sh", cmd])
      if rc:
          raise Exception("demo_panel.sh %s failed (exit %d)" % (cmd, rc))
  demo_panel("start")
  ```

  and `demo_panel("stop")` in the `finally:` block if the test should leave no panel behind.

## Run a test without the IDE window

```
java -cp oculixide-5.0.0-complete-win.jar org.sikuli.ide.Sikulix -r host/ide-tests/MyTest.sikuli
```

Close the IDE first. On Linux (UBUNTU24, OculiX 870a7ff4 QTB-1 build) a run beside the open IDE crashes one or both
within seconds, in OculiX's hotkey listener (`jxgrabkey.JXGrabKey.listen`, SIGSEGV), and the IDE's unsaved scripts
are lost. On a Linux tester, `host/poc/run-test.sh` checks this and says PASS or FAIL.
