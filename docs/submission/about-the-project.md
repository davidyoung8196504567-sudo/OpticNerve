## Inspiration

The screens that matter most are the hardest to test. An infusion pump, a car's instrument cluster, a factory control panel: their HMIs run on real-time operating systems like QNX, and a wrong value or a missed alarm on one of them can cost a life. Yet they are still tested largely by hand, tap by tap, because desktop GUI test tools can't reach inside an embedded target. Those tools need a JVM, a desktop and an input API that a real-time OS doesn't provide.

Oculix, the open-source successor of SikuliX, already tests GUIs the way a human does: it **sees** the screen with image matching and OCR, and **acts** on it. What it lacks is a way into the target. OpticNerve is that way in: a nerve between the tester's eye and the device's screen.

## What it does

*Gives your test tools eyes and hands on any QNX 8.0 screen.*

The **ON agent** is a small native C program that runs on the QNX 8.0 target. It:

- captures the display through QNX Screen,
- injects touch, mouse and keyboard input,
- serves both over VNC, so Oculix 4.x on a Windows or Linux laptop can see the screen, tap buttons, read text and report pass or fail,
- runs as a QNX resource manager (`/dev/opticnerve`), controlled through native message passing.

No Java on the target. It works at the Screen level, so it can test an app built with any GUI toolkit. The demo target is a Raspberry Pi 5, with the same test suite running against a QEMU target.

*Status: day one. The agent doesn't exist yet. The ground it will stand on does.*

## How we built it

We started where a tool for safety-relevant testing has to start: with something we can trust. Before writing a line of agent code, we built two QNX SDP 8.0 build hosts, one on Windows 11 and one on Ubuntu 24.04, and put them under strict configuration management. Every QNX package and exact version is pinned in git. A check script independent of the vendor's installer hashes every installed file against the SHA-512 values in QNX's own package descriptors:

$$\forall f \in \text{SDP}:\quad \operatorname{SHA512}(f) = h_f$$

That is 24,743 files on Windows and 22,862 on Linux, and the 265 packages common to both hosts are byte-identical. The check also builds a test program for AArch64 and x86_64 targets. A host is used only when it prints `MATCHES THE BASELINE`.

The agent will be built in phases: survey Screen's capture and input APIs, stream frames, inject input, wrap it all in a resource manager, then write the Oculix test suite against a demo HMI.

## Challenges we ran into

On day one, all of them came from the toolchain, not the code:

- **A package that silently overwrote newer files.** One Screen utilities package ships older copies of 20 files that two newer packages also install, including the VNC server. The installer gave no warning; our check caught it. The kind of quiet failure that slips through is exactly what OpticNerve is meant to catch on the screen side.
- **Learning a new package manager from scratch,** including its patch sets, and finding that a patch set import uninstalls and reinstalls the whole SDP rather than adding to it.
- **Reproducing an identical SDP on a second host.** It took several rounds before the hashes matched.

Ahead of us: capturing frames fast enough on a Pi 5 without disturbing the app under test, and finding out what permissions screen capture and input injection need under QNX's security policies.

## Accomplishments that we're proud of

- Two build hosts, Windows and Linux, verified file by file against the same baseline.
- A reproducible SDP baseline that anyone can check with one command.
- A toolchain that builds for both targets: AArch64 for the Pi 5 and x86_64 for QEMU.
- A brand and a plan to the deadline.

## What we learned

- QNX 8.0 ships its own VNC server for Screen. Whether OpticNerve builds on it or brings its own is the first design decision, and it shapes everything after it.
- "Installed successfully" and "installed correctly" are not the same thing. Verify, don't trust.
- QNX's packaging carries a SHA-512 for every file it installs, which made independent verification possible.

## What's next for OpticNerve

The next milestone: the Pi 5's screen appearing in a stock VNC viewer on a laptop, streamed by the ON agent. After that, the first automated tap, and then the first Oculix test that goes green against real QNX hardware.

Stay tuned. The eye is about to open.
