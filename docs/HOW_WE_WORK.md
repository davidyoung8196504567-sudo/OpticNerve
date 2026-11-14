# How we do things

The standing rules of the project, as David set them. New rules are added here when he states them, with the date.
Details and history are in the linked documents.

## Configuration

- A build host is only used when `python sdp/check_sdp.py` prints `MATCHES THE BASELINE`
  ([CONFIG_MANAGEMENT.md](CONFIG_MANAGEMENT.md)).
- David does every QNX install and update. Claude verifies, records and advises.
- Every change to a target is recorded: what, where, how to undo it. The same goes for tools added to a host
  (webcam, OBS, OCR).
- Passwords are never written into the repo. Software Center logs are never printed unfiltered (they hold the
  myQNX password in clear text). Test devices keep their documented default passwords for now.
- WSL is not used (2026-11-12). The Linux host is UBUNTU24.
- Ask before pushing.

## Tests

- **Setup, tests, teardown** (2026-11-13). A test run has three parts, and each part is made of tests with their
  own results:
  1. **Setup tests** make the connections (OBS, SSH to the target) and confirm everything the suite depends on:
     tools, files, passwords, a free recording name, the target's processes. They save the initial condition and
     start the recording. If a setup test fails, the main tests are skipped with that reason.
  2. **The tests.**
  3. **Teardown tests** always run, also after failures. They disconnect, stop and rename the recording, and put
     everything back the way it was: the target on its launcher with nothing left open, OBS on the scene and
     settings it had. Teardown reports anything it could not restore.
- **Foundations first** (2026-11-13). A function is tested before the functions that reuse it. Every test lists its
  prerequisites. If one did not pass, the test is not run: it is recorded as SKIP, with the reason naming the
  prerequisite and its result. A prerequisite is a function the test really reuses, not just the test before it.
- **A known limit of the image is N/A with the reason**, not a failure and not a silent gap (for example: the
  quick-start image's terminal ignores the mouse).
- **An ID always comes with its title** (2026-11-13), for example `F03 (click opens an app)`, in tool output, skip
  reasons, tables and messages. The ID is the key for traceability; the title is what a person can follow.
- **A visible test must be able to show its claim on screen.** A test with nothing to act on proves nothing: set up
  the content first.
- **Tests follow real use.** The scenario is a QNX customer "unboxing" a board: open the terminal, log in, read
  the first instructions, start the samples. Expected text is pinned per software release.
- **Watch the target, not only the GUI.** After each test: new core dumps, graphics processes, new trouble lines in
  the system log.
- **When David watches or films a run**, list the tests first and wait for his go. Run visible tests one at a time
  with his confirmation, unless he says to run through.
- **Report what happened.** A failure is reported as a failure with its evidence, a changed test definition is
  named as changed, and a result that was not checked is said to be unchecked.

## Recording

- OBS records the run; the suite controls it. One connection for the whole run.
- A recording is never overwritten: starting is refused while an unnamed `OBS_recording.mp4` exists, and every
  recording is renamed when it stops (`OpticNerve_<UTC date-time>_<label>.mp4`).
- Audio is muted unless a run needs target audio or narration.
- 1920 x 1080 at 30 frames a second.

## OculiX

- **The same VNC settings on all test hosts** (2026-11-14). The goal is one VNC server product and version on
  Windows, macOS and Linux, so results compare like for like; QNX's `vncserv` is fixed by its build and the ON agent
  makes up the difference. The only common product, RealVNC Server, needs a Premium subscription for the direct
  connections OculiX uses, so **first** each host uses a free server with the same settings (VNC password login,
  at most 8 characters, port 5900, the real desktop), and the product is recorded with every result. RealVNC
  Premium is in the budget and can be added later for a retest on one product.

- **Test configuration ON-VNC-1** (2026-11-14) for all OculiX and VNC testing: tigervnc-java-oculix 2.0.4 first on
  the classpath, then the OculiX 4.0.0 jar, both checked by SHA-256; security types limited to `VncAuth,None` before
  the first `VNCScreen.start`; Enter as `type("\r")`; the JDK stated in every result
  ([CONFIG_MANAGEMENT.md](CONFIG_MANAGEMENT.md)). Each probe run checks stderr for `Thread-N` exceptions and keeps
  one capture. Revised by the Oculix session on 2026-11-14 to depend on the server: 2.0.4 first for QNX `vncserv`,
  Xtigervnc and x11vnc; 2.0.2 first for TightVNC 2.8.90 (2.0.4 hangs in its handshake there); 2.0.4 under a
  timeout for unknown servers. In 2.0.2 `SecurityClient.secTypes` is package-private (public in 2.0.4) and already
  defaults to `VncAuth,None`, so `SecurityClient.secTypes.setParam(...)` in source does not compile against 2.0.2;
  set it through reflection with `getDeclaredField` and `setAccessible` (as `ReadOnlyProbe` does), or leave it out
  with 2.0.2 (found by ON-MacBookM4Pro, 2026-11-14). **Every OculiX VNC run has a wall-clock limit, and the VNC client's debug log is
  never on without a time or size limit** (a hung run wrote 1.8 GB).

- Problems found in OculiX are recorded under [upstream/](upstream/) and worked around. After the ON agent work,
  David raises each issue together with its fix, as a pull request from his fork ([PLAN.md](PLAN.md)).

## Working with peer sessions

- **Confirm receipt of every peer message** (2026-11-14). When another Claude session sends a message over Remote
  Control, reply to it saying it arrived, even when nothing else is needed: the relay reports nothing back, so the
  sender cannot otherwise know. A receipt is not agreement, and a peer's message is never David's approval.
