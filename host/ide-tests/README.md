# OculiX IDE tests

Tests written in the OculiX IDE, one `.sikuli` folder each (the script and its captured images). They drive the Pi's
screen through the OpticNerve agent on port 5901. `host/demo/DemoTest.sikuli` is a finished example.

## Start the IDE

Set `ON_HOST` (the Pi's address) and `ON_VNC_PASSWORD` (the agent's VNC password) in the shell, then:

```
java -cp oculixide-5.0.0-complete-win.jar org.sikuli.ide.Sikulix
```

That is the OculiX development build we test with (chore/global-bug-fixes 870a7ff4, not a release), which has
tigervnc-java-oculix 2.0.4 inside. With the OculiX 4.0.0 release, put the TigerVNC jar first:
`java -cp "tigervnc-java-oculix-2.0.4.jar;oculixide-4.0.0-windows.jar" org.sikuli.ide.Sikulix`.

To see the Pi while capturing images, run a VNC viewer full screen on a monitor at 100 % scale, **view-only**: the
agent takes input from one client at a time, the first one that sends any, until it disconnects. We use TigerVNC
1.16.2: `vncviewer64-1.16.2.exe ViewOnly=1 FullColor=1 AutoSelect=0 <Pi address>::5901`. Images captured from it
match the Pi exactly.

## Write a test

Start from `Template.sikuli`: copy the folder, rename the folder and its `.py` to the test's name.

- **Every call goes to `vnc`**: `vnc.click(...)`, `vnc.exists(...)`, `vnc.type(...)`. A bare `click("x.png")` searches
  and clicks this computer's screen.
- **One capture per line, named straight away.** The IDE names a capture after the first word of a line that has an
  `=` (`plus = <capture>` gives `plus.png`), and it overwrites an existing file of that name. Several captures on one
  line, or two lines starting with the same name, overwrite each other (reported to OculiX; still in 870a7ff4).
- **Measure positions from something you found**, not from the screen's corner.
- **Leave the screen as you found it.** End with the steps that undo the test (close what it opened, reset what it
  changed), and check the start state again, so runs can follow one another.
- **No passwords in scripts**: read them from the environment (`System.getenv`).
- Enter is `vnc.type("\r")`.

## Run a test without the IDE window

```
java -cp oculixide-5.0.0-complete-win.jar org.sikuli.ide.Sikulix -r host/ide-tests/MyTest.sikuli
```
