# OpticNerve server design: the test agent for QNX 8.0 and its RFB server

Status: **draft for review R-001, gate G1 (Design)**. Written 2026-11-15 by the `designer` role and revised the same
day with David's decisions (section 5.4), so the choices below are decided. The review has not been held: gate G1
still needs its two reviews and the disposition. No server code exists. David gave the direction on 2026-11-15: "go
for G1, our own VNC server".

If the review accepts it, this replaces the decision of [DESIGN.md](DESIGN.md) (2026-11-12: "pixels and input go
through QNX's own `vncserv`"). DESIGN.md stays as the earlier design, kept for the record; it now points here and
uses the tool's new name. The gates are defined in [REVIEWS.md](REVIEWS.md). This document is the item for gate G1
(Design). Sections 4.2 and 4.3 are written to become the item for gate G3 (Concurrency), and sections 4.7 to 4.9
the starting point for gate G2 (Protocol).

## How to read this document

Every statement of fact carries one of these marks. A statement of what the design will do is a decision, not a fact,
and carries none.

| Mark | Meaning |
|---|---|
| **[measured: file]** | we ran it and the named file holds the result |
| **[doc: source]** | read from documentation or a standard; the source is named |
| **[inference]** | reasoned by the author; not measured |
| **[not measured]** | nothing to go on yet |

Sources and their short names:

| Short name | Source |
|---|---|
| MATRIX | [OCULIX_VNC_MATRIX.md](OCULIX_VNC_MATRIX.md) |
| ISSUES | [ISSUES.md](ISSUES.md) |
| FIRSTBOOT | [notes/2026-11-12_pi5_first_boot.md](notes/2026-11-12_pi5_first_boot.md) |
| CONFIG | [CONFIG_MANAGEMENT.md](CONFIG_MANAGEMENT.md) |
| TAP | the RFB tap log of the Windows → Pi input runs of 2026-11-14 and 15: 317 connections of OculiX under ON-VNC-1. It is kept off this repository because it also holds login keystrokes. Only message types, sizes and the client's set-up messages were read for this document |
| RFC | RFC 6143, "The Remote Framebuffer Protocol" (<https://www.rfc-editor.org/rfc/rfc6143.txt>), by section number |
| QNX-API | QNX SDP 8.0 Screen API reference, public pages under <https://www.qnx.com/developers/docs/8.0/com.qnx.doc.screen/topic/>: `screen_inject_event()`, `screen_read_display()`, `screen_notify()`, `screen_create_window()`, `screen_create_device_type()`, "Screen context types", "Screen notification types" (pages dated 2026-11-05, read 2026-11-15) |
| QNX-GUIDE | Screen Graphics Subsystem Developer's Guide, public pages under the same address, folder `manual/`: "Permissions and Privileges", "Asynchronous Notifications", "Function safety", "Input", "Security policies", "Utilities and binaries", `vncserv` |
| QNX-LIB | QNX SDP 8.0 C Library Reference and Utilities Reference, public pages: `resmgr_attach()`, `select_attach()`, `pulse_attach()`, and the `on` utility |
| LICENCE | QNX Development License Agreement (Non-Commercial License), v7, 2025-12-10, read in full from the fleet's library copy, which is not in this repository. Paraphrased here, by section number |
| LIBVNC | the LibVNCServer repository on GitHub (<https://github.com/LibVNC/libvncserver>): its licence, releases and issue titles as the GitHub API returned them on 2026-11-15 |

An ID always carries its title, for example ON-OX-004 (2.0.4 loops on a split security-type list). ON-VNC-1 is the
adopted client configuration of [HOW_WE_WORK.md](HOW_WE_WORK.md): tigervnc-java-oculix 2.0.4 first on the classpath,
security types limited to `VncAuth,None`. DESIGN.md numbered its spikes S1 to S6. This document adds seven, from S7
(input injection reaches an application) to S13 (encoding cost).

**What this document leaves out, by rule.** David's rule of 2026-11-15, in his words: "no judgment of the QNX 8
software or infrastructure (eg. myqnx or qnx software center) shall be stored on the repo. Instructions of what we
used are permitted because it allows judges to reproduce our results." [HOW_WE_WORK.md](HOW_WE_WORK.md) carries it
as a standing rule. So this document names the QNX components, versions, API calls and settings the design uses,
and says what the design requires of them. It reports no defect, limit, measurement, timing or comparison of QNX
software. Where the design needs a number, it gives a requirement of our own. Results about our own server are
public.

## 1. Summary

1. **Decided (David, 2026-11-15): alternative C, deployed as alternative D.** The program on the target is a test
   agent that speaks RFB: a small RFB 3.8 server of our own and the agent of [PLAN.md](PLAN.md) in one process,
   single-threaded on a QNX dispatch loop that also serves the `/dev/opticnerve` resource manager. It is one binary,
   `opticnerve`. The server is started as `opticnerve serve`, and status and control are subcommands of the same
   binary (section 4.10). It listens on port 5901 **beside** `vncserv`, the server the quick-start image starts on
   port 5900. We leave `vncserv` running and untouched, and our program never replaces or disables it. All of it is
   original work under the repository's MIT licence, and the repository publishes source only.
2. **Alternative B is not pursued** (a server built on LibVNCServer), and it is not kept as a fallback. It would make
   the server GPL-2.0-or-later, it raises a question under the QNX licence, it needs a cross-build for QNX that we
   have not tried, and it brings far more protocol code than our three clients need. What B has and C lacks (every
   encoding, every pixel format) is not asked for by any requirement. The fallback is alternative A.
3. **What is not yet shown: that a server of ours can deliver input.** `screen_inject_event()` has never been run in
   this project **[not measured]**. The first deliverable is therefore a spike, S7 (input injection reaches an
   application), not server code. It runs first so that a risk to the input path is known early (section 8). Until
   our server is ready, OculiX reaches the Pi through the image's `vncserv`, as it does today.
4. **Also settled on 2026-11-15** (section 5.4). Any user on the target may read the agent's status and use its
   everyday controls; starting it, stopping it and its VNC password are for root (section 4.11). The build baseline
   is SDP-BL-006 on the Windows laptop, which is the project's only SDP development host (section 4.13). The
   keyboard that gives the keymap its reference is named (section 4.9). Capture speed gets its target at gate G4
   (Test plan), not here (section 4.6). A test that fails against our server is repeated against a second,
   independent RFB server, TigerVNC on UBUNTU24 (section 7). The repository holds no judgment of QNX software
   (section 4.14).

## 2. The problem and requirements

### 2.1 The problem

A test tool on a host (OculiX 4.0.0 under ON-VNC-1) must see a QNX 8.0 screen, act on it, and know whether what it
sees is current. Today OculiX reaches the Pi through `vncserv`, the server the quick-start image starts on port
5900, with the client configuration ON-VNC-1 on the host. We leave `vncserv` running and untouched.

The project wants the RFB side of a test connection to be something it specifies and tests itself: the pixel format,
when a frame is sent, what each key symbol becomes, and what happens when a client comes or goes. It also wants the
agent's features of [PLAN.md](PLAN.md) in the same place. David, 2026-11-15 (PLAN.md): "it's actually easier to build
a custom VNC server to work with OculiX". Section 2.2 states this as requirements of our own.

The table holds what the design starts from on the client's side and in our own project:

| Topic | Result | Evidence |
|---|---|---|
| What OculiX's client sends at set-up | version 3.8; security type 2; shared session; `SetPixelFormat` 32 bits per pixel, depth 24, shifts 16, 8, 0; `SetEncodings` `[-307, -224, -313, -312, -258, -1063131698, 16, 1, 7, 5, 2, 1, 0, -254]`. ZRLE (16) is the first real encoding and Raw (0) is in the list | [measured: TAP, identical in all 317 connections] |
| How OculiX asks for frames | 9,842 update requests in these runs, all for the whole 1920 x 1080 screen: 6,668 incremental and 3,174 non-incremental. The non-incremental ones come mostly as back-to-back pairs (1,219 pairs) | [measured: TAP] |
| Which encoding OculiX has been seen to receive | only ZRLE, in every run so far. Whether its Raw decoder gives correct pixels is open | [measured: MATRIX "Encodings" and the Mac probe] [not measured] |
| The security-type message | tigervnc-java 2.0.4 loops when the count and the list arrive in separate writes: ON-OX-004 (2.0.4 loops on a split security-type list) | [measured: MATRIX, TightVNC rows; ISSUES] |
| More security types than VncAuth | 2.0.4 takes the first type it supports: ON-OX-005 (2.0.4 picks Apple DH) | [measured: MATRIX, row "Windows → Mac", configuration C] |
| 16 bpp servers | wrong colours and `ClassCastException` with OculiX 4.0.0 alone: ON-OX-002 (rgb565 decoded with wrong colours), ON-OX-003 (find and text throw for a 16 bpp server) | [measured: MATRIX] |
| ZRLE at 32 bpp | corrupt or black frames with the client bundled in OculiX 4.0.0: ON-OX-001 (ZRLE at 32 bpp corrupts frames in the bundled client). Correct with 2.0.2 and 2.0.4 | [measured: MATRIX] |
| `screen_inject_event()` | never run in this project | [not measured] |

### 2.2 Requirements

| # | Short title | Requirement | Traced to |
|---|---|---|---|
| R1 | login without looping | OculiX under ON-VNC-1 logs in: the security-type count and list in one write, VncAuth the only type offered | ON-OX-004 (2.0.4 loops on a split security-type list), ON-OX-005 (2.0.4 picks Apple DH); ISSUES "server-side" |
| R2 | 32 bpp natural format | The server's own pixel format is 32 bpp true colour, so no client is handed a 16 bpp frame unless it asks | ON-OX-002 (rgb565 decoded with wrong colours), ON-OX-003 (find and text throw for a 16 bpp server) |
| R3 | exact pixels | The pixels sent are exactly the pixels Screen composed | PLAN.md "Pixel format and encodings OculiX decodes correctly" |
| R4 | a decodable encoding | An encoding OculiX decodes correctly, and a setting with which the server never sends ZRLE | ON-OX-001 (ZRLE at 32 bpp corrupts frames in the bundled client): "our server can avoid triggering it" (ISSUES) |
| R5 | frames only on change | Frames are sent only when the screen changed, and a test can ask whether a frame is current | PLAN.md "only send a frame when Screen composed one" |
| R6 | input through Screen | Pointer and keys reach applications through Screen. The entries ISSUES marks server-side are handled: ON-OX-007 (Key.DELETE sends keypad Delete), ON-OX-011 (lowercase keysym sent with Shift held), ON-OX-012 (KeyModifier.META sends Meta_L); and Alt and Meta combinations are delivered to applications | ISSUES "Who can fix it"; PLAN.md "Server-side key handling" |
| R7 | guardrail | Correct only what a server should; never compensate for OculiX bugs. ON-OX-006 (Key.ENTER sends Linefeed), ON-OX-008 (Windows key codes read as Java key codes), ON-OX-009 (Button.* constants do nothing; bare mouseUp leaves the button held) and ON-OX-010 (paste and non-ASCII type send only Ctrl+V) must stay visible | PLAN.md "Guardrail"; ISSUES |
| R8 | agent features | In the same process: freshness, idle and power state, keep-awake, region capture, the `/dev/opticnerve` resource manager, component versions on screen and in the RFB desktop name | PLAN.md; DESIGN.md |
| R9 | clients come and go safely | Any client may connect or disconnect at any moment and in any protocol state without harming the server or another client | REVIEWS.md, gate G3 (Concurrency) |
| R10 | recorded undo | Every change to a target is recorded with its undo | HOW_WE_WORK.md |
| R11 | builds and runs on QNX 8.0 | It builds with qcc for aarch64le and x86_64 and runs on QNX 8.0 on the Pi 5 | hackathon rules (section 2.3) |
| R12 | original and open source | Original work under an open-source licence; nothing of QNX's in the repository | hackathon rules; LICENCE 4.6(g) |
| R13 | QNX-specific technology | A resource manager, message passing and pulses, and the Screen API, each doing real work | hackathon bonus prize |
| R14 | fits the time | Done by 2026-12-15 with gates G2 (Protocol) to G5 (Release) still to pass | PLAN.md |
| R15 | testable | Everything that is not Screen can be tested without a Pi, and every acceptance test can fail | REVIEWS.md "Lessons carried over" |
| R16 | beside vncserv | It runs beside `vncserv`, the server the quick-start image starts on port 5900, and never replaces, disables or disturbs it | David, 2026-11-15 (section 5.4) |

### 2.3 Hackathon rules that bind the design

From <https://qnx.devpost.com/rules> as recorded in [CONTEXT.md](CONTEXT.md) and [PLAN.md](PLAN.md) on 2026-11-12
[doc: those two files]. The rules page was not read again for this document.

- Deadline 2026-12-15, 14:00 Eastern.
- Original work, solely owned by the entrant. Open-source pieces are allowed if their licences are followed. No
  proprietary third-party code or assets.
- A public repository with an open-source licence shown on the repository page, all source, and build and run
  instructions naming the QNX version, target and toolchain. It must really build for and run on QNX 8.0.
- The free QNX Everywhere licence only.
- A bonus prize for the best use of QNX-specific technologies: message passing, resource managers, the microkernel,
  boot customisation, real-time services, security policies.

## 3. The alternatives, smaller ones first

This section is the record of what was weighed, which gate G1 (Design) asks for. David decided on 2026-11-15:
alternative C, deployed as D (section 5.4). The alternatives that keep `vncserv` as the path for pixels and input
(A0, A1, A and E) are described by what the project would and would not specify and test itself.

### A0. Change nothing: `vncserv` as it is, ON-VNC-1 on the host

- **What:** the state of today.
- **Cost:** none.
- **What the project then specifies itself:** nothing on the target. The pixel format, the timing of frames and the
  handling of keys are those of `vncserv`. There is no agent, so R8 (agent features) is not met, and a test has
  nothing to ask whether a frame is current, which is the second half of R5 (frames only on change).
- **Licence:** no question. **Schedule:** no work. **Hackathon:** nothing of ours runs on the target, so R11 (builds
  and runs on QNX 8.0) and R13 (QNX-specific technology) are not met.

### A1. `vncserv` started with another pixel format

- **What:** one documented option, `-format`, sets the pixel format `vncserv` reads back. Its documented default is
  rgb565, and the image starts `vncserv` without the option [doc: QNX-GUIDE `vncserv`; FIRSTBOOT for the start line].
  `-format=rgba8888` would be set where the image starts `vncserv`. That is a recorded target change.
- **What it gives:** a 32-bit format on port 5900, which is what R2 (32 bpp natural format) asks of a server. With
  OculiX 4.0.0 alone, a 32 bpp server takes the client from ON-OX-002 (rgb565 decoded with wrong colours) and
  ON-OX-003 (find and text throw for a 16 bpp server) to ON-OX-001 (ZRLE at 32 bpp corrupts frames in the bundled
  client) [inference: from ISSUES]. The option has not been tried here **[not measured]**.
- **Cost:** a recorded change to the way the image starts `vncserv`. **What could break:** not known
  **[not measured]**.
- **Position:** not part of the design, which leaves `vncserv` running and untouched, started as the image starts
  it.

### A. Keep `vncserv` and build the agent beside it (the accepted design of 2026-11-12)

- **What:** the ON agent as a resource manager with freshness, state and region capture, reached over SSH. Pixels and
  input stay with `vncserv`.
- **Meets:** R8 (agent features), and R10 (recorded undo) to R13 (QNX-specific technology). It meets the second half
  of R5 (frames only on change) as a side channel: a test asks the agent whether the screen changed.
- **What the project still does not specify:** the RFB side. R1 (login without looping) to R4 (a decodable
  encoding), the first half of R5 (frames only on change) and R9 (clients come and go safely) would be statements
  about `vncserv`, not about code of ours. For R6 (input through Screen), a key table of our own could only live in
  an agent helper. That helper needs `screen_inject_event()` just as B and C do, and the tests would call our helper
  where they should call OculiX.
- **Risk:** the suite's eyes and hands are a QNX binary that the project sets up through its options and does not
  change [doc: DESIGN.md, "Risks and tunables"].
- **Licence:** no question; the repository stays MIT. **Schedule:** the least code of all alternatives that meet R11
  (builds and runs on QNX 8.0). **Hackathon:** meets the rules; the "eyes and hands" remain QNX's.
- **Position:** the fallback if C fails (section 8).

### E. A proxy in front of `vncserv`

- **What:** our RFB tap grown into a proxy that applies a key table and a frame-timing rule of our own to the stream.
- **Why not:** it needs an RFB parser, a frame decoder and an encoder, which is most of the protocol code of C. The
  pixels and the input would still pass through a server the project does not specify, and the agent would still be
  a second program [inference]. Listed for completeness.

### B. Our own server built on a pinned LibVNCServer release

- **What:** LibVNCServer does the protocol; our code does capture, injection and the agent. The library can be driven
  from our own loop without its per-client threads [inference: from the library's public interface as the author
  knows it; the source was not read for this document].
- **Facts read:** licence GPL-2.0 as GitHub reports it (PLAN.md records GPL-2.0-or-later). Newest release 0.9.15,
  published 2024-12-22, tag commit `9b54b1ec32731bd23158ca014dc18014db4194c3`; 0.9.14 was published 2022-12-18. Its
  open issues include two about its threaded mode and its event loop, #456 and #615 [doc: LIBVNC; read by title
  only, not analysed against our use]. The public collection of QNX ports (`qnx-ports/build-files`, 151 entries on
  2026-11-15) has no entry for it [doc: GitHub API listing of that repository].
- **Fixes:** R1 (login without looping): x11vnc, which is built on the library, works with 2.0.4 [measured: MATRIX;
  CONFIG records x11vnc 0.9.16 with libvncserver 0.9.14]. R2 (32 bpp natural format), R3 (exact pixels), and R5
  (frames only on change) through its hooks [inference]. R6 (input through Screen) and R8 (agent features) with our
  own code, as in C.
- **Cost:** a cross-build for QNX with qcc that we have not tried [not measured]. About 40,000 lines of third-party C
  running as root on the target, much of it for features we would switch off (file transfer, web sockets, the HTTP
  server, scaling) [inference: not counted]. A patch, or living with the client's preference, where R4 (a decodable
  encoding) asks for a setting that never sends ZRLE.
- **What could break:** the port; behaviour inside code we did not write and cannot review in the time; and any use
  of the library that departs from the single thread of section 4.2.
- **Licence:** the server as a whole becomes GPL-2.0-or-later. Linking it with QNX's `libscreen` raises a question
  under LICENCE 14.3 (section 5). B is not pursued, so the question was not put. The repository would carry two
  licences.
- **Schedule:** unknown until the cross-build is tried, and the licence decision would come first. **Hackathon:**
  allowed, but the protocol engine would not be original work, and the entry would read as a port.
- **Position:** not pursued, and not kept as a fallback (David, 2026-11-15; section 5.4, Q3 (B kept open)).

### C. A small RFB 3.8 server of our own

- **What:** our own code for the handshake, VncAuth (with our own DES), six client messages, Raw encoding, and later
  ZRLE through zlib if measurements ask for it. An estimated 2,500 lines of C including the agent [inference].
- **Fixes:** R1 (login without looping) to R9 (clients come and go safely) by construction, each to be shown by a
  test (section 7). R15 (testable): the protocol core has no QNX dependency and is tested and fuzzed on a host.
- **Cost:** we write and test every byte of the protocol we use. Three or more of the four weeks that remain.
- **What could break:** a protocol mistake that only shows with a client we did not test; Raw being too slow for the
  host on a 100 Mb/s link (section 4.6); and, as for B and for A's key helper, input injection.
- **Licence:** all original work; MIT stays; no copyleft question. **Schedule:** fits if S7 (input injection reaches
  an application) passes in the first week and the gates do not queue (section 8). **Hackathon:** the strongest fit
  for R12 (original and open source) and R13 (QNX-specific technology).
- **Position:** the design (David, 2026-11-15; section 5.4, Q1 (C beside vncserv)).

### D. Our server on another port, beside `vncserv`

- **What:** a way to deploy, not a third engine. Our server listens on port 5901. `vncserv` keeps port 5900, its
  password file, its start at boot and its binary.
- **Why:** nothing on the target is replaced or disabled, so the undo is "stop our process and delete our folder".
  OculiX goes on reaching the Pi through `vncserv` until our server is ready (R16 (beside vncserv)). If our server
  fails at any stage, the suite still has A.
- **Cost:** the tools need a port argument (`vnc_snapshot.py` and `vnc_input.py` have 5900 built in). Two programs
  then read the display, and both can inject input. That our agent and `vncserv` do not disturb each other is
  **[not measured]** (section 6, U10 (two servers at once)).
- **Position:** the way C is deployed (David, 2026-11-15; section 5.4, Q1 (C beside vncserv)).

### F. Two processes: a root agent and an unprivileged RFB server

- **What:** the network-facing protocol code runs as an ordinary user. It gets pixels and sends input through the
  resource manager. Only the small agent is root.
- **Why it is attractive:** less code runs as root, and QNX message passing does real work (R13 (QNX-specific
  technology)).
- **Cost:** frames cross a process boundary; two processes to start and to watch; more ways to fail; several more
  days [inference].
- **Position:** not for the deadline. The module boundary of section 4.1 is drawn so that this split stays possible.

### 3.1 Comparison

| | A0 | A1 | A | B deployed as D | C deployed as D |
|---|---|---|---|---|---|
| Login of OculiX with 2.0.4 | the adopted configuration ON-VNC-1 | as A0 | as A0 | yes (inference) | yes (to be shown by test) |
| A pixel format the project specifies: R2 (32 bpp natural format), R3 (exact pixels) | no | one documented option | as A0 or A1 | yes | yes |
| Frame timing the project specifies: R5 (frames only on change) | no | no | no; a side channel answers "is it current" | yes | yes |
| A key table the project specifies and OculiX itself uses: R6 (input through Screen) | no | no | no: a helper beside OculiX | yes, if injection works | yes, if injection works |
| Client handling the project specifies: R9 (clients come and go safely) | no | no | no | partly | yes |
| Original work, MIT | not applicable | not applicable | yes | no: GPL engine | yes |
| Tests without a Pi | no | no | agent only | little | the whole protocol core |
| New code we own (estimate) | 0 | 0 | about 800 lines | about 1,200 lines plus the port | about 2,500 lines |
| Licence decision needed first | none | none | none | LICENCE 14.3 with the GPL | none |
| Biggest unknown | none | its effect, which was not tried | the key helper needs injection | the cross-build | injection; the speed of Raw |

The rows say what the project would specify under each alternative.

### 3.2 What was weighed, and the decision

- **Seeing.** Under A0, A1 and A the pixels come from a server the project does not specify. R3 (exact pixels) and
  R5 (frames only on change) ask for behaviour that the project wants to define and to test, and only B and C put
  that behaviour into code of ours.
- **Acting.** The entries that ISSUES assigns to the server side call for a key table of our own, and so for an input
  path of our own. That path has never been tried, and this holds for A's helper, for B and for C alike. So S7
  (input injection reaches an application) is run before anything else.
- **B or C.** B's advantage is completeness, and the requirements do not ask for it: three clients (OculiX, our
  Python tools, one stock viewer for the demo), one pixel format that OculiX asks for, one or two encodings. B's
  costs are certain or unknown: a licence change, a question under LICENCE 14.3, a cross-build we have not tried.
  C's main cost is known: we write the protocol code, and it is small because the requirements are small.
- **`vncserv` stays.** D leaves it running and untouched on port 5900. Our program never replaces or disables it.
- **Decision (David, 2026-11-15): C, deployed as D.** B is not pursued and is not kept as a fallback. The fallback
  is A (section 8). The protocol engine still sits behind a narrow interface (section 4.1). The reasons for that
  are tests on a host (R15 (testable)) and the later split of alternative F, not a change of engine.

## 4. The design

### 4.1 Overview

```
 host                                             target (QNX 8.0, Pi 5)
 +----------------------+   RFB 3.8, port 5900   +--------------------------------------------------+
 | OculiX / our tools   | <--------------------> | vncserv (the image's own), untouched             |
 |                      |                        +--------------------------------------------------+
 |                      |   RFB 3.8, port 5901   | opticnerve serve (ours: one process, one thread) |
 |                      | <--------------------> |   net:    sockets, non-blocking                  |
 |                      |                        |   core:   RFB engine (portable C)                |
 | helpers over SSH     |   SSH, port 22         |   screen: capture, notify, inject, label         |
 |  -> opticnerve ...   | ---------------------> |   resmgr: /dev/opticnerve                        |
 +----------------------+                        +--------------------------------------------------+
                                                   opticnerve status, wait, capture, keepawake, label (the
                                                   same binary, run by any user): open, read, devctl on /dev/opticnerve
```

OculiX uses port 5900 until our server is ready, and port 5901 from then on. `vncserv` keeps running throughout,
and nothing of ours touches it.

Modules, and the one rule between them: **the core does no input or output and includes no QNX header.**

| Module | Contents | Builds for |
|---|---|---|
| `core/` | the RFB state machine, fed with bytes and returning bytes; DES for VncAuth; pixel conversion; tile compare; encoders; the keysym table; counters | QNX, and any host with a C11 compiler |
| `qnx/` | the dispatch loop, sockets, Screen (capture, notification, injection, label window), the resource manager, logging | QNX only |
| `cli/` | the subcommands that talk to the agent through `/dev/opticnerve`: `status`, `wait`, `capture` and the others of section 4.10 | QNX only |
| `tests/` | host tests and a fuzz harness for `core/`; a **fake server** (`core/` with a synthetic framebuffer) for OculiX tests without a Pi; a **target test application** that shows known pixels and logs every input event it receives | host; QNX |

`core/`, `qnx/` and `cli/` are linked into one binary, `opticnerve` (section 4.10). From here on, "the agent" means
the running `opticnerve serve` process: the RFB side and the features of section 4.10 together.

### 4.2 Process and thread model

**Decision: one process, one thread, one place where it waits.**

| Alternatives, smaller first | Cost | What could break |
|---|---|---|
| **T1: one thread on a QNX dispatch loop** (chosen) | a slow handler delays everything else | a handler that blocks |
| T2: T1 plus one worker thread for compression | one object crosses threads; needs its own review at gate G3 (Concurrency) | the lifetime of a job whose client closes |
| T3: one or two threads per client | every client record is shared between threads | a record freed while another thread still uses it |
| T4: two processes (alternative F) | days of extra work | more ways to fail |

Why T1:

- QNX's dispatch layer delivers resource manager messages, pulses and file-descriptor readiness through one call,
  `dispatch_block()`: `resmgr_attach()`, `pulse_attach()` and `select_attach()` all take the same dispatch handle
  [doc: QNX-LIB `resmgr_attach()`, `pulse_attach()`, `select_attach()`; the `resmgr_attach()` page gives a
  single-threaded resource manager as its example]. That `select_attach()` does what is needed with network sockets
  is **[not measured]** (S8 (sustained capture and the dispatch loop)).
- QNX's guide says that with asynchronous notifications "all event handling can be directed to one thread", which
  makes ordering simple [doc: QNX-GUIDE "Asynchronous Notifications"].
- With one thread no object is shared between threads. R9 (clients come and go safely) then reduces to rules about
  lifetime inside one thread (section 4.3), and those rules can be tested.
- Screen's functions are thread-safe but not signal-safe [doc: QNX-GUIDE "Function safety"]. With T1 the first does
  not matter, and the second is met because signal handlers call nothing.

The events the loop receives:

| Event | How it arrives | What the handler does |
|---|---|---|
| `open`, `read`, `devctl`, `close` and unblock requests on `/dev/opticnerve` | resource manager message | status text, control, snapshots, waiters |
| Screen completed a composition | pulse, from `screen_notify()` with `SCREEN_NOTIFY_UPDATE` on the display | sets `dirty`, counts |
| Screen has events queued (display change, idle) | pulse, from `screen_notify()` with `SCREEN_NOTIFY_EVENT` | drains the queue with a zero timeout |
| Timer tick, armed only while a client or a waiter exists | pulse | deadlines: handshake, stalled client, waiters, safety capture |
| Listening socket readable | `select_attach()` | accepts, or refuses when full |
| Client socket readable | `select_attach()` | reads, feeds the core |
| Client socket writable, armed only while output is queued | `select_attach()` | sends from the client's queue |
| SIGTERM, SIGINT | the handler sets one flag, and the interrupted `dispatch_block()` returns [inference] | clean shutdown |

After every event the loop runs two steps, in this order: **reap** (free the clients marked closed), then
**schedule** (decide captures and updates, section 4.5).

What may block, and for how long: `screen_read_display()` "blocks until the operation is completed" [doc: QNX-API].
The limit this design sets for any handler is 100 ms, and it requires a whole-display read to stay under that limit.
S8 (sustained capture and the dispatch loop) checks that it does. Injecting an event and setting a property are
message exchanges with Screen [inference]. Sockets are non-blocking. The loop writes no files: `opticnerve capture`
writes its file as the calling user (section 4.10).

T2 is added only if a measurement shows a handler of ours holding the loop for more than 100 ms; ZRLE of a full frame
is the candidate. It would come with a written hand-over rule and a second review at gate G3 (Concurrency).

Logging goes to the system logger, which the suite already reads with `slog2info` [measured:
`tests/visible/suite.py`]. No key value, password, challenge or response is ever logged.

### 4.3 Ownership of every object (the item for gate G3 (Concurrency))

**Locking: none.** One thread owns everything, so there is nothing to lock and no lock order. The only code that runs
outside the loop is the signal handler, and it touches one flag. "Owner" below means the module whose functions are
the only ones that touch the object.

| Object | Created | Owner | Freed | Notes |
|---|---|---|---|---|
| Dispatch handle, channel, connection for pulses | start | loop | exit | |
| Privileged Screen context (display manager and input provider), display handle, event object | start | screen | exit, in reverse order of creation, after notifications are disabled and the event is unregistered | created again if Screen goes away and returns |
| Application Screen context, the label window and its buffer | start | screen | exit | a separate context, so the window is an ordinary application window |
| Capture pixmap and its buffer | start, at the display's size | screen | exit; created again on a size change, after every client is closed | filled only by `screen_read_display()` |
| The current frame `cur` (our copy, padding byte cleared) and the table `tile_seq` | start | frame | exit; created again with the pixmap | the only pixels the core ever sees |
| Keyboard and pointer input devices | start | screen | exit | only if S7 (input injection reaches an application) shows that devices are needed |
| Listening socket | start | net | exit | |
| **Client record**: socket, protocol state, a small fixed input buffer, the output queue, pixel format, pending requests, `sent_seq`, the keys and buttons this client holds, deadlines, later a zlib stream | accept | net: a fixed table of slots, each with a generation number | **in one place only, `client_free()`**, run by the reap step | see the rules below |
| Open-file record of `/dev/opticnerve`, with its snapshot buffer | `open` | resmgr | the close handler, which the resource manager library also calls when the client process dies [inference: standard behaviour of the library; to be confirmed at gate G3 (Concurrency)] | a bounded number |
| Waiter (a caller blocked in "wait for update"): its receive ID and deadline | `devctl` | resmgr | when it is answered: on a composition, a timeout, an unblock request, or the close of its file | each receive ID is answered exactly once |
| VncAuth key | read at start from a root-only file | auth | cleared at exit | never logged, never returned |

Rules, each with the test that can show it broken (tests are in section 7):

| # | Short title | Rule | Test |
|---|---|---|---|
| I1 | handlers do not block | No handler blocks, except in the Screen calls named in section 4.2 | RB03 (a stalled client does not delay others) |
| I2 | deferred free | A client is closed by marking it. `client_free()` runs only in the reap step, never inside a handler that still uses the record | RB01 (connect and disconnect in every state), with the AddressSanitizer build |
| I3 | no stored pointers | No pointer to a client record is kept outside the table. Callbacks carry a slot number and a generation. A callback for a freed slot finds a different generation and does nothing | RB01 (connect and disconnect in every state); HT04 (unit tests of the core) |
| I4 | the one place a client is freed | `client_free()` does, in this order: detach the socket from the dispatch handle; release the keys and buttons the client holds; close the socket; free the queue and the stream; increase the generation | IN09 (a held button is released when its client disconnects) |
| I5 | bounded memory | At most 4 clients, one queued update each, a fixed number of open files and waiters | RB04 (memory and descriptors flat over a soak) |
| I6 | deadlines | A client that does not finish the handshake in 10 s, or does not read for 30 s while output is queued, is closed | HS06 (half-open handshake is closed); RB03 (a stalled client does not delay others) |
| I7 | errors stay local | An error on one client closes that client only. An error from Screen closes all clients; the agent keeps running and tries Screen again | RB05 (Screen restarted under the agent) |
| I8 | interrupted calls | Calls that can return `EINTR` are retried | code review; RB05 (Screen restarted under the agent) |
| I9 | nothing trusted | Nothing a client sends is trusted: lengths are checked before use, and an unknown message type closes the client | HT03 (fuzzing the core); HS07 (unknown message type) |

The numbers in I5 (bounded memory) and I6 (deadlines) are first values, to be confirmed with measurements.

### 4.4 Capture path and pixel format

**Decision: one capture path. `screen_read_display()` reads the whole display into a 32-bit pixmap, and the result is
compared tile by tile with our copy.**

| Alternatives, smaller first | Cost | What could break |
|---|---|---|
| **Whole-display read into a pixmap** (chosen) | one blocking call for each capture; the design requires it to stay under the handler limit of section 4.2 | a read that takes longer than the limit: S8 (sustained capture and the dispatch loop) checks it |
| Region reads for small changes | a second path through Screen, whose bytes would have to be shown equal to the first for every screen | two paths, two sets of bytes to test |
| A virtual or mirrored display with shared buffers | a change to the target's graphics configuration, which this design does not make | not looked into further |

- The context must be a display manager context, and creating one needs effective root [doc: QNX-API
  `screen_read_display()` and "Screen context types"]. The buffer must have the native usage flag [doc: QNX-API]. The
  pixmap has the display's size, so no scaling takes place [doc: QNX-API: scaling is applied when the sizes differ].
- Format: `SCREEN_FORMAT_RGBA8888`. In memory the bytes of a pixel are blue, green, red, alpha [measured: our probe
  `agent/spikes/screen_probe.c` writes the byte order down]. This is byte for byte the RFB format "32 bits per pixel,
  depth 24, little-endian, red shift 16, green shift 8, blue shift 0", and it is what OculiX asks for under ON-VNC-1
  [measured: TAP; the tap does not record the byte-order flag]. So the server's natural format (RFC 7.3.2) needs no
  conversion for OculiX. Test PX01 (every patch of the test application has exactly its known value in a raw-tool
  frame) checks the byte order for the server.
- "Exact" means equal to what `screen_read_display()` returns. The server draws nothing into a frame.
- The fourth byte of a pixel is padding in RFB. It is set to zero in `cur`, so frames are reproducible to the byte.
- `cur` is compared with each new capture in tiles of 64 x 64 pixels. A tile that differs is copied into `cur` and
  stamped with a new content number in `tile_seq`. Each client remembers the content number it was last brought up
  to (`sent_seq`). An update for a client is the set of tiles stamped later than its `sent_seq`. One copy of the
  frame serves all clients.
- A region for the agent (section 4.10) is cut from `cur`. Every consumer sees the same bytes.
- Other client pixel formats (RFC 7.5.1): true-colour formats of 8, 16 and 32 bits are converted by one general
  routine. A request for a colour map closes the connection with a logged reason. To be confirmed at gate G2
  (Protocol).
- A display that changes size closes all clients and creates the buffers again. OculiX does not announce that it
  could follow a size change [measured: TAP: neither -223 nor -308 is in its list].
- Virtual displays, and more than one display, are out of scope and untested.

### 4.5 When frames are sent

**Decision: a non-incremental request is answered at once from a fresh capture. An incremental request is answered
only when Screen has signalled a composition and the captured pixels differ from what that client has.**

The protocol allows exactly this. For a non-incremental request the server sends the area "as soon as possible". For
an incremental one it sends "if and when there are changes", after "an indefinite period". And one update "may be sent
in reply to several" requests [doc: RFC 7.5.3]. OculiX works with TigerVNC (configuration C of MATRIX) and with x11vnc
(ON-VNC-1) [measured: MATRIX]. Both hold back incremental requests on a static screen [inference: the author's
knowledge of those servers, not confirmed with a tap].

| Alternatives, smaller first | Cost | What could break |
|---|---|---|
| A full frame for every request | none in code; 8.29 MB a frame | no freshness: R5 (frames only on change) is not met |
| Send on every composition signal | simple | the design takes a signal to mean "the screen may have changed", not "there is one new frame", so the same frame could be sent more than once |
| **Signal, then capture, then compare** (chosen) | the tile compare | a change that Screen does not signal: see the safety capture below |
| Poll and compare at a fixed rate, no signal | a read every period, changed or not | processor load; a delay of up to one period |

The schedule step, in order:

1. All input from all readable sockets is consumed first. The pairs of non-incremental requests that OculiX sends
   [measured: TAP] thereby become one update.
2. A client with a non-incremental request pending gets a capture taken now, and the whole requested area clipped to
   the framebuffer. ON-OX-013 (update requests sized as corners) sends areas far larger than the screen [measured:
   MATRIX]; they are clipped. A request that is empty after clipping gets no update.
3. If `dirty` is set, a client has an incremental request pending, and 33 ms have passed since the last capture:
   capture, compare, and send each such client the changed tiles inside its area. A client for which nothing changed
   keeps its request pending. An update with no rectangles is never sent.
4. A new client, and a client that has just changed its pixel format, has `sent_seq` zero. Its first request is
   answered in full whatever its flag says.
5. **Safety capture.** A client whose incremental request has been pending for 2 s without a signal gets one capture
   and compare. If that finds a change, the update is sent and the counter `unsignalled_changes` goes up. The premise
   "no signal means no change" is thus checked all the time and reported (section 4.10), not assumed.
6. A client with output still queued gets no further update until its queue is empty.

What a test can rely on: every update comes from one capture. The status reports the number of composition signals,
the content number, and for each client the content number last sent.

### 4.6 Encodings

**Decision: Raw first. ZRLE with zlib is added only if a measurement shows that it is needed. A setting keeps the
server to Raw.**

- Raw is always permitted: "pixel data may always be sent in raw encoding even if not specified explicitly", and
  every client must be able to handle it [doc: RFC 7.5.2 and 7.7.1]. OculiX's list contains Raw [measured: TAP]. That
  OculiX decodes Raw frames correctly is **[not measured]** (S9 (OculiX accepts Raw)).
- The cost of Raw: a full frame is 1920 x 1080 x 4 bytes, which is 8.29 MB. UBUNTU24's wired link is 100 Mb/s, and
  the Windows host's Wi-Fi link is 780 Mb/s [measured: MATRIX, table of test hosts]. So a full frame takes at least
  0.66 s on the first and at least 0.09 s on the second [inference: arithmetic at the link rate; what Wi-Fi really
  carries is less]. Small changes are small in Raw: one changed tile is 16 KB.
- **Capture time.** How long OculiX needs for a full-screen capture from our server is measured and reported for
  each test host: S13 (encoding cost) first, then test FR06 (a full-screen OculiX capture from each test host).
  This document sets no target for it (section 5.4, Q13 (capture speed)). Gate G4 (Test plan) sets the target, from
  our own measurements. The expected basis is that a capture has to fit inside one step of a step-and-check test,
  which is about one second. By the arithmetic above a Raw frame fits inside that on both hosts, with little room
  on the 100 Mb/s host [inference]. The figure S13 (encoding cost) measures decides whether ZRLE is added.
- ZRLE: one zlib stream for each client for the life of its connection [doc: RFC 7.7.6], owned by the client record.
  zlib's header and library are among the SDP's target files [measured: `zlib.h` and `libz.so` in the build folder of
  baseline SDP-BL-006]. We link to it and ship nothing of it. Its cost on the Pi is **[not measured]** (S13 (encoding
  cost)).
- ZRLE at 32 bpp is what the client bundled with OculiX 4.0.0 cannot decode: ON-OX-001 (ZRLE at 32 bpp corrupts
  frames in the bundled client). The server does not try to recognise clients. It has one option, `-encodings=raw` or
  `-encodings=raw,zrle`, and each test result states which was used. This is a server choosing among the encodings a
  client offered. It is not a repair of the client, so R7 (guardrail) holds.
- CopyRect, RRE, Hextile and Tight are not implemented. The extensions OculiX announces with the negative numbers in
  its list are ignored, which is what a server without them does [doc: RFC 7.5.2]. A client may send a message of an
  extension only after the server has confirmed that extension [doc: RFC 7.5]. So a client message that belongs to an
  extension we never confirmed closes that client, by rule I9 (nothing trusted). OculiX did send one such message, of
  the extended clipboard, in 160 of the 317 connections of the tap log [measured: TAP]; that it sends none to a
  server that does not confirm the extension is **[inference]**, and test HT05 (OculiX `ReadOnlyProbe` against the
  fake server) shows it.

### 4.7 Handshake and security types

| Step | What the server does | Basis |
|---|---|---|
| Version | sends `RFB 003.008\n` and accepts 3.8 from the client. In the first deliverable anything else is answered with a failure reason and closed. Whether 3.7 and 3.3 are served is decided at gate G2 (Protocol) | RFC 7.1.1 |
| Security types | **one buffer of two bytes, `01 02`, handed to the socket in one call**: one type, VncAuth. `TCP_NODELAY` is set on every client socket. Type None is never offered | RFC 7.1.2; R1 (login without looping) |
| VncAuth | 16 random bytes from the system's random source; the 16-byte answer is compared in constant time | RFC 7.2.2 |
| Result | `0`; or `1` with a reason string, in one buffer, and then the connection is closed | RFC 7.1.3 |
| ClientInit | the shared flag is read and ignored. Sessions are always shared: a viewer can watch while a test runs, and no client disconnects another | RFC 7.3.1 |
| ServerInit | width, height, the format of section 4.4, and the desktop name `OpticNerve <version> <host>:<display>` in ASCII | RFC 7.3.2; R8 (agent features) |

- Every server message is assembled in one buffer and handed to the socket in one call. That two bytes handed over in
  one call reach the client in one segment is **[inference]**; test HS02 (security types arrive in one segment) looks
  at the wire.
- VncAuth is weak by design: it "is not intended for use on untrusted networks" [doc: RFC 7.2.2]. The server is for a
  test network. Its VNC side does not start without a password file, and there is no built-in password. The file is
  ours (`/data/opticnerve/etc/vncpasswd`, owner root, mode 600). `opticnerve passwd`, run as root, writes it, reading
  the password from standard input and never from the command line, and asks the agent to read the file again. The
  agent takes that request from root only (section 4.11). `vncserv`'s own password file is neither read nor written.
- DES is implemented by us from the public standard (FIPS 46-3). It is checked against published test vectors and
  against the answers our Python tools compute.
- A fifth client is refused with a reason. Failed logins are counted in the status. A delay after repeated failures
  from one address is left to gate G2 (Protocol).

### 4.8 Input injection

**Decision: a privileged context of the input provider type, and `screen_inject_event()` on the display. Whether the
events are tied to input devices that we create is decided by S7 (input injection reaches an application).**

What the documentation says:

- `screen_inject_event()` needs a privileged context, of the window manager type or the input provider type. It
  "sends the event to the window that has input focus on the specified display" [doc: QNX-API
  `screen_inject_event()`].
- The design uses the input provider type. It does not ask for the window manager type: that role belongs to the
  window manager the image runs, and the design leaves it there.
- An input provider context needs write access to `/dev/screen/.provider`. An application context with write access
  to `/dev/screen/.inject` can inject events as well. Context types are bits that can be combined, so one context
  can be both display manager and input provider [doc: QNX-API "Screen context types"]. That the combination works on
  the target is **[not measured]**.
- An input provider can create keyboard and pointer devices with `screen_create_device_type()` [doc: QNX-API].
- "Input events are always sent to only one recipient (usually the window or session that has focus)" [doc: QNX-GUIDE
  "Input"]. How an injected pointer event finds the window under its position is **[not measured]**. S7 (input
  injection reaches an application) settles it.

| Alternatives, smaller first | Cost | What could break |
|---|---|---|
| No injection: our server only shows the screen | none | the entries ISSUES assigns to our server (R6 (input through Screen)) then have no code of ours behind them, and OculiX cannot act through our server |
| **Input provider context and `screen_inject_event()`** (chosen, to be proven) | small | how pointer events are routed; access to `/dev/screen/.provider` |
| The same, with a keyboard device and a pointer device of our own | a little more | unknown interplay with the real devices |
| An application context with access to `/dev/screen/.inject` | a permission change on the target | one more target change to record |
| `screen_send_event()` to one process | needs to know the receiving process; the design holds no window manager context and so keeps no list of windows [inference] | not general |

State the server keeps: for each client, the buttons and keys it holds down. They are released when the client
disconnects, by rule I4 (the one place a client is freed), so a client that vanishes cannot leave the target with a
held button or modifier. Other servers do the same for the same reason [inference: the author's knowledge of
TigerVNC's server; to be confirmed at gate G2 (Protocol)]. The status shows what is held and by which client, so a
test can assert that nothing is held. This is the "held button visible to the agent" of PLAN.md.

Pointer: position and button mask as RFC 7.5.5 gives them; buttons 4 to 7 become wheel steps. Which way a wheel step
goes is taken from a physical mouse on the target in S11 (keyboard and mouse reference), not assumed. Touch contacts
are out of scope.

### 4.9 The keymap and the guardrail

**Principle: for a keysym, the server injects the event that a keyboard plugged into the same target produces for
the key carrying that symbol. It never turns one key into another.**

The reference is measured, not assumed. S11 (keyboard and mouse reference) records, with the target test application,
the events of the USB keyboard on the Pi (key cap, symbol, scan code, modifiers, flags) for every key in the table.
The acceptance tests compare our injected events with that record. The record itself is test evidence about a QNX
component and is kept in the private review record. The keymap table in our source states what the server injects.

The physical devices (David, 2026-11-15): a USB keyboard and a USB mouse have been attached to the Pi the whole
time. The keyboard is a Logitech Classic Keyboard 200, model Y-UR83, part number 868017-0403. Screen's log names a
Logitech USB keyboard with the USB ID 046d:c315. The mouse's model is not recorded yet; S11 (keyboard and mouse
reference) records it. The keys the reference needs:

- the letters and the digits, each alone and with Shift, and Caps Lock;
- Return, Tab, Shift+Tab, Backspace, Escape, the space bar and the function keys F1 to F12;
- Insert, Delete, Home, End, Page Up, Page Down and the four arrow keys;
- the numeric keypad: every key with NumLock off and with NumLock on, and the keypad's Enter;
- the left and the right Shift, Ctrl and Alt keys, AltGr where the layout has it, the Super keys and the Menu key;
- one letter with each of Ctrl, Alt, Super, Ctrl+Shift and Ctrl+Alt held.

Whether the Y-UR83 has every key on this list is checked at the start of S11 (keyboard and mouse reference)
**[not measured]**. A key it lacks is taken from another of David's keyboards, for example a separate numeric
keypad, and the record names the keyboard used for each key.

What the standard says a server should do [doc: RFC 7.5.4]:

- The case of a keysym is significant, and the Shift state is only a hint. An upper-case "A" without Shift is an "A".
- Lock keys should be ignored where possible; each character keysym is interpreted according to its case.
- Control and Alt, unlike Shift, modify the interpretation of other keys.
- A server should accept `ISO_Left_Tab` as a shifted Tab.

How QNX names keys [doc: the SDP header `sys/keycodes.h`, package `target.input.keyboard`, as baseline SDP-BL-006
installs it; described here, not copied]: characters are their Unicode values; other keys have `KEYCODE_` names in a
private range; modifiers are bits with `KEYMOD_` names. There are key names for Shift, Ctrl, Alt and "Hyper", and
none for a Meta key or a Super key. The keypad keys are named after their navigation function. So the X keysyms for
keypad navigation, and those for Meta and Super, have no key of the same name on the QNX side, and the table has to
say what they become.

| Entry in ISSUES | Side | What the server does | Basis |
|---|---|---|---|
| ON-OX-007 (Key.DELETE sends keypad Delete) | server | the keysym `KP_Delete` becomes the keypad's Delete key as a keyboard sends it with NumLock off; the other keypad navigation keysyms likewise | the X convention that the `KP_` navigation keysyms are the keypad without NumLock [inference]; S11 (keyboard and mouse reference) gives the exact event |
| ON-OX-012 (KeyModifier.META sends Meta_L) | server | `Meta_L`, `Meta_R`, `Super_L` and `Super_R` become the key, and set the modifier, that the target's keyboard produces for its Super key | S11 (keyboard and mouse reference); the header has no key named Meta |
| Alt and Meta combinations (PLAN.md "Server-side key handling") | server | modifier keys are tracked, and every key event carries the modifier bits, as a keyboard's events do | RFC 7.5.4, third point above |
| ON-OX-011 (lowercase keysym sent with Shift held) | server; **the rule is open, for gate G2 (Protocol)** | see below | RFC 7.5.4, first two points above |
| ON-OX-006 (Key.ENTER sends Linefeed) | client | Linefeed has no key on a PC keyboard and no QNX key name. It is not injected. It is counted in `keys_unmapped`. It is **not** turned into Return | R7 (guardrail) |
| ON-OX-008 (Windows key codes read as Java key codes) | client | "." arrives and is typed as "." | R7 (guardrail) |
| ON-OX-009 (Button.* constants do nothing; bare mouseUp leaves the button held) | client | the mask is applied as sent. The button stays held while the client is connected, and the status shows it | R7 (guardrail); rule I4 (the one place a client is freed) covers disconnects only |
| ON-OX-010 (paste and non-ASCII type send only Ctrl+V) | client | Ctrl+V is delivered; there is no clipboard support | R7 (guardrail) |

**The open point: ON-OX-011 (lowercase keysym sent with Shift held).** PLAN.md lists "the letter keysym upper-cased
when Shift is held". The standard says the opposite for plain typing: the keysym's case counts and Shift is a hint.
x11vnc and TightVNC give "a" [measured: MATRIX, input slots 1 and 2], and TigerVNC's server presses Caps Lock to the
same end [measured: MATRIX, row "UBUNTU24 → TigerVNC depth 16": Caps Lock is left on]. A server that upper-cases
would differ from all three in order to change what an OculiX call produces, and that is what R7 (guardrail) forbids.
Two rules are possible:

- (a) Deliver what was sent: the symbol "a", with the modifier bit for Shift set. `type("a", SHIFT)` gives "a", as on
  x11vnc. With Ctrl or Alt also held, the application receives the chord with all its modifiers and decides.
- (b) Upper-case a letter whenever Shift is held, as PLAN.md says.

This design implements the rule as one switch and recommends (a) on the evidence above. The decision belongs to gate
G2 (Protocol), with the Oculix session. Under either rule the test records what was sent and what arrived.

Keys are never logged: a log of keys would hold the target's login password. The server counts key events. For a
keysym outside the character ranges that it could not map, it keeps the last value.

### 4.10 The agent's features, in the same process

The resource manager is registered at `/dev/opticnerve` [doc: QNX-LIB `resmgr_attach()`], as in DESIGN.md.

| Feature | How | Evidence and limits |
|---|---|---|
| Status | `read()` returns text: versions; the agent's process ID; display size; clients; composition signals; content number; time of the last change; `unsignalled_changes`; for each client the content number sent and the keys and buttons held; idle state; power mode; keep-awake; counters for unmapped keys and failed logins | it holds no secret |
| Freshness | the counters above. The `devctl` "wait for update" blocks its caller until the next composition or a timeout: the reply is deferred, and the waiter is removed on reply, timeout, unblock or close (section 4.3) | it rests on `screen_notify()` with `SCREEN_NOTIFY_UPDATE` [doc: QNX-API]. Tests FR01 (static screen for 10 s), FR02 (one patch changes) and FR03 (`opticnerve wait` returns on a change and times out without one) show what the agent does with it |
| Idle and power state | read on request from the display's properties `SCREEN_PROPERTY_IDLE_STATE` and `SCREEN_PROPERTY_POWER_MODE` [doc: DESIGN.md, "The ON agent"] | they are reported as Screen states them. The agent adds no reading of its own |
| Keep-awake | the label window's `SCREEN_PROPERTY_IDLE_MODE` is set to `SCREEN_IDLE_MODE_KEEP_AWAKE` while it is switched on [doc: DESIGN.md, "The ON agent"] | it acts on Screen's idle timer, whose setting belongs to the target's configuration, and not on a desktop's own lock. It is kept because it costs a few lines on a window we have anyway. Test AG02 (keep-awake counter 0, 1, 0) shows it |
| Region capture | the `devctl` "snapshot x y w h" cuts the region from a fresh capture into the caller's open-file record and returns size, format and content number. `read()` then returns the pixel bytes. `opticnerve capture` writes the file itself, as the calling user | the agent never opens a path that a caller names, so a caller cannot make a root process write a file |
| Versions on screen | a small window above the launcher's bottom bar. Its text is drawn into its buffer with a bitmap font of our own, in a 32-bit format. The text is the agent's version plus a string set with `opticnerve label`, for example the name of the test run. The agent does not read other programs' files | where the window lands under the image's window manager is **[not measured]** (S10 (label window placement)). If it cannot be placed, the version is still in the desktop name and the status. Tests leave the label's rectangle out of pixel comparisons, as DESIGN.md says |
| Versions in the desktop name | section 4.7 | read with `vnc_snapshot.py` |

**One binary, named `opticnerve`** (David, 2026-11-15). The server and the commands that talk to it are subcommands
of one program, and the resource manager keeps the same name in its path, `/dev/opticnerve`. The name stays well away
from `on`. QNX has a standard utility called `on`, in `/bin`: "Execute a command in a non-default way" [doc: QNX-LIB,
the `on` utility] [measured: `bin/on` among the target files of the build folder]. A name built on those two letters
can be read as related to that utility. DESIGN.md and PLAN.md use the new name as well.

The subcommands, stated once. Options have the form `-name=value`.

| Subcommand | Run by | What it does |
|---|---|---|
| `opticnerve serve -display=1 -port=5901` | root | runs the agent: the RFB side and `/dev/opticnerve`. It is the only subcommand that creates a Screen context, a socket or the device. `-encodings=` is the option of section 4.6 |
| `opticnerve status` | any user | prints the status text |
| `opticnerve wait -timeout=2` | any user | blocks until the next screen update, or until the given number of seconds has passed |
| `opticnerve capture X Y W H FILE` | any user | writes a region of the screen to FILE, as the calling user |
| `opticnerve keepawake on` or `off` | any user | switches keep-awake |
| `opticnerve label TEXT` | any user | sets the text of the label |
| `opticnerve stop` | root | asks the agent to shut down cleanly. The agent refuses this request from any other caller |
| `opticnerve passwd` | root | writes the VNC password file from standard input and asks the agent to read it again (section 4.7). The agent refuses the reload from any other caller |

Run with no subcommand, or with one it does not know, the program prints its usage and exits with an error. It never
starts a server by default. The host reaches the subcommands over SSH, as DESIGN.md planned for its tool. No
subcommand injects input: input comes through the VNC connection alone.

What one binary costs and what could break [inference]. The subcommands that only talk to the device carry the
server's code and load its libraries. Every `opticnerve` process has the same name whatever its subcommand, so a test
cannot find the agent by name, and `slay opticnerve` ends them all. The status therefore gives the agent's process
ID. Test AG07 (no subcommand starts nothing) covers the default.

### 4.11 Privileges and access

- `opticnerve serve` runs as root, as `vncserv` is started on the quick-start image (`vncserv -display=1 -auth`, as
  root) [doc: CONFIG, row "VNC server on the Pi target"]. The display manager context needs effective root [doc:
  QNX-API "Screen context types"], and registering a name needs the path-space ability [doc: QNX-LIB
  `resmgr_attach()`].
- It asks for two context types only: display manager and input provider. It does not ask for the power manager type:
  the agent reads the power mode and never changes it. That reading needs no more is **[inference]**.
- Dropping root after start-up is S12 (dropping root); it is not planned for the deadline.
- QNX security policies are not addressed, as in DESIGN.md. The README says so.
- Every other subcommand runs as the calling user and holds no privilege of its own.

**Access (David, 2026-11-15).** Any user on the target may use `status`, `wait`, `capture`, `keepawake` and `label`.
Only root may use `serve`, `stop` and `passwd`, and only root may make the agent read the VNC password file again.
The node `/dev/opticnerve` belongs to root and has mode 666, with no group restriction. For the stop request and
for the password reload the agent checks that the caller is root and refuses anyone else. The resource manager
library gives a handler the caller's credentials [measured: `iofunc_client_info_ext()` is declared in the baseline's
`sys/iofunc.h`; how the check is written is settled at gate G3 (Concurrency)].

| Alternatives, smaller first | Cost | What could break |
|---|---|---|
| Every user, for everything | none in code | any user can stop the agent in the middle of a run, or make it read the password file again |
| Root only, for everything | every helper call needs root on the target | the suite's helpers log in as `qnxuser` (DESIGN.md, "Interface") |
| **Every user for status and the everyday controls; root for `serve`, `stop`, `passwd` and the password reload** (decided) | a check of the caller on two requests | what follows |
| One group for the controls, and root for `stop` and `passwd` (the first draft) | the same check, and a group to choose | users outside the group; one more rule to keep and to test |

What this means, said plainly:

- Any user who can log in to the target can capture the screen through the device without the VNC password
  (`opticnerve capture`). The same user can read the status, which shows the keys and buttons that are held, can
  wait for updates, and can switch keep-awake and set the text of the label.
- Only root can start the agent, stop it, or set and reload its VNC password. The suite does these steps through
  `sudo`, as it does its other root steps [measured: `tests/visible/suite.py`].
- Input stays with the RFB connection. The device has no command that injects input, and an RFB connection needs the
  VNC password, from a client on the target as from one on the network.
- The quick-start image is a single-user test target (David, 2026-11-15). The README says all of this.
- A deployment that needed more would narrow the permissions of the device node: its owner, its group and its mode.
  Beyond those permissions the design has one check, the check for root on the two requests above.

### 4.12 Start, stop, and putting the target back

| Step | Change on the target | Undo |
|---|---|---|
| Install | one folder, `/data/opticnerve/`, with `bin/opticnerve` and `etc/`, copied by `host/tools/opticnerve_deploy.py`, which records the SHA-256 of each file. Programs run from that partition [doc: CONFIG, row "Spike test program on the Pi target": the probe ran from `/tmp`, which is on it]. The target stays the quick-start image: nothing of ours is built into an image | `opticnerve_deploy.py --undo`: stop the agent, delete the folder |
| Password | `/data/opticnerve/etc/vncpasswd` | deleted with the folder |
| Start, in the first deliverable | by hand, or by the suite's setup tests over SSH, as root: `opticnerve serve -display=1 -port=5901`. Nothing is added to the boot sequence, so a reboot also returns the target to its state | `opticnerve stop`, as root; or `slay opticnerve`, which also ends any other `opticnerve` subcommand running at that moment [inference: `slay` selects by name] |
| Start at boot: later, and only with David's go | one line added to the image's `post_startup.sh`, calling a script of ours; the original file is saved with its checksum | restore the saved file. FIRSTBOOT names it as `/system/etc/startup/post_startup.sh`; the path is read on the target first |
| `vncserv` | none: its port 5900, binary, password file and start at boot are untouched | none needed |

Every install, and a start at boot if it comes, is entered in CONFIG like the other target changes. The suite's
teardown test TD01 (target back to its initial condition) checks that the agent is stopped or as it was found, that
`vncserv` has the process ID it had at setup, and that no core dump is new.

At exit the agent releases held keys and buttons, destroys its devices and its window, and removes its name. What
state a held button is left in if the agent is killed outright is **[not measured]**; test RB06 (the agent is killed
while a button is held) finds out.

### 4.13 Build

- **The build host** (David, 2026-11-15): the Windows laptop is the project's only SDP development host. The SDP
  installation on UBUNTU24 will be removed; UBUNTU24 stays a test client and a reviewer. A second SDP installation
  may later go on the HC8000 server when it is online. Every QNX build in this document is made on the Windows
  laptop.
- **The baseline** is SDP-BL-006, in the folder `C:\Users\root\qnx805`, which is the only SDP installation on that
  host [doc: CONFIG, "Baseline history"]. It is the SDP 8.0.5 release as QNX ships it, installed with the Software
  Center's update policy "Ultraconservative", plus `quickstart.rpi5` 0.5.0.00050, `target.utils.gdbserver`
  14.2.0.00328 and `target.screen.vncserv` 4.2.0.00070, and two packages those depend on, `osr.libyuv` and
  `screen.img_codecs`. That makes 236 packages, with the fingerprint `9556e4536e637ff3`. A program built from it for
  aarch64le ran on the Pi 5, whose kernel carries the same build date (2026-06-05) as the baseline's microkernel
  package [doc: CONFIG]. The Pi 5 BSP and the Screen board packages are deliberately not installed: we deploy onto
  the quick-start image and do not build an image.
- A build host is used only when `python sdp/check_sdp.py` prints `MATCHES THE BASELINE`
  ([HOW_WE_WORK.md](HOW_WE_WORK.md)).
- `make` with qcc and nothing else. `qcc -Vgcc_ntoaarch64le` first, `-Vgcc_ntox86_64` second. Both are built on
  every build, so "builds for x86_64" stays true. Flags: `-std=c11 -Wall -Wextra -Werror -O2`. Libraries: `screen`,
  `socket`, and `z` once ZRLE exists. The result is one binary for each target, `opticnerve`.
- The keymap needs `sys/keycodes.h`, which comes with the package `target.input.keyboard`. The baseline has both
  [measured: the header in the build folder; the package in `sdp/packages.lock`].
- Running on x86_64 needs an x86_64 QNX target with Screen, and none is set up ([PLAN.md](PLAN.md), "Hardware pool").
  The baseline has the x86_64 files to build against [measured: `libscreen.so` for x86_64 in the build folder] and
  no packages for a virtual target [doc: CONFIG]. Until a target exists the claim for x86_64 is "builds", and the
  README says exactly that.
- `make host` builds `core/` and its tests with the host's own C compiler, and `make fuzz` runs the fuzz harness on
  Linux. Neither needs an SDP, so they run on the test clients, UBUNTU24 among them. Whether each test client has a
  C11 compiler set up for this is **[not measured]**.
- Two more target variants for testing, built separately: one with AddressSanitizer and one with the
  undefined-behaviour sanitizer. The baseline has both runtimes for aarch64le [measured: `libasan.so` and
  `libubsan.so` in the build folder]. That they run on the Pi image is **[not measured]**.
- The version comes from one file, `agent/VERSION`, and the commit ID. No build time is embedded, so the same source
  gives the same binary.

### 4.14 What goes in the repository and what must not

| In | Not in |
|---|---|
| our source, the Makefile, the host tests, the deploy script, this document, the hashes of built binaries (in CONFIG), test patterns we draw ourselves, the bitmap font we draw ourselves | QNX binaries, libraries or headers, text copied from QNX headers or documentation, QNX images (LICENCE 4.6(g)) |
| constants we define for X keysyms and RFB numbers, with values from the RFC and the X protocol | a copy of QNX's key code values: the code includes `sys/keycodes.h` by name and uses the names |
| instructions of what we used, so that a judge can reproduce our results: the QNX components and their versions, the API calls, the settings | any judgment of QNX software or infrastructure: a defect, a limit, a measurement, a timing or a comparison (David's rule of 2026-11-15; section 5.4, Q10 (no judgment of QNX software)) |
| results about our own server, for example its frame rate and its processor time (section 5.4, Q12 (results about our own server)) | results of an evaluation of QNX software (section 5.1, LICENCE 4.6(i)) |
| | a built `opticnerve` binary, or a target image. A binary contains start-up and library code from the SDP [inference], and LICENCE 4.2.2 ties distributing it to an end-user agreement (section 5.4, Q2 (source only)) |
| | LibVNCServer or any other third-party source; alternative C needs none |
| | passwords, tap logs and core files |

## 5. Licence constraints

The author is not a lawyer. Every row below was **read from the document** named. The column "Bearing on the design"
is the author's reading. The decisions are David's.

### 5.1 QNX Development License Agreement (Non-Commercial), v7

| Section | What it says, paraphrased | Bearing on the design |
|---|---|---|
| 4.1(iii) | The SDP may be used, linked and compiled to develop, test and demonstrate non-commercial applications and target systems | covers the agent under A, B and C |
| 4.1(iv) | The SDP may be used for interoperability testing so that open-source software can be ported, developed and maintained to work with the SDP, provided that software is made public | supports a public open-source server |
| 4.2.2 | No commercial use. Demonstration, and limited distribution for demonstration, of non-commercial applications is permitted if the recipient agrees to the end-user agreement in Exhibit B | the repository publishes source only: no built binary and no target image |
| 4.6(c) | No reverse engineering, decompiling or disassembling | the design rests on public documentation, the RFC and behaviour seen from outside. Nothing in it comes from inside a QNX binary |
| 4.6(d) | No modifying software delivered as binaries | `vncserv` is not touched |
| 4.6(g) | No distributing the software to third parties | section 4.14 |
| 4.6(i) | No publishing the results of a performance or functional evaluation of the software without written approval | the repository publishes no result of an evaluation of QNX software. It reports our own server (section 5.4, Q10 (no judgment of QNX software) and Q12 (results about our own server)) |
| 4.6(m) | No use of the software in connection with developing competitive products | the program is a test agent for QNX targets that speaks RFB. It runs beside `vncserv`, which stays running and untouched, and it never replaces or disables `vncserv` |
| 5.1 | Open-source software inside the SDP is identified for each file in the Software Center ("OSS Compliance") and stays under its own licence | zlib on the target is such software. We link to it and ship nothing of it |
| 9.2 | Software the developer writes independently belongs to the developer, subject to BlackBerry's rights in the SDP | C is solely owned original work |
| 10.1 | Performance data and benchmark results about the software are treated as confidential information | handled as for 4.6(i) |
| 14.3 | The developer must not grant rights that would place QNX's software under an open-source licence that requires source disclosure, licensing for derivative works, or redistribution at no charge | no question for C, which is MIT. For B, a GPL program linked with `libscreen` raises exactly this question. B is not pursued, so it was not put |

### 5.2 GPL obligations for alternative B

Kept as the record of what was weighed; B is not pursued. From the GNU General Public License, version 2, as the
author knows it [inference: the licence text was not read again for this document].

| Obligation | What it means for B |
|---|---|
| A work that contains GPL code is licensed as a whole under the GPL | the `opticnerve` binary would be GPL-2.0-or-later. Our own files could stay MIT; the program could not |
| Keep the copyright notices and the licence text; mark changed files | applies to the library, whether it is copied into the repository or fetched at build time |
| Whoever distributes a binary must offer the complete corresponding source | we distribute source only, so this falls on anyone who distributes a build |
| The exception for libraries that normally come with the operating system | whether `libscreen` falls under it is the question behind LICENCE 14.3. The author cannot answer it |

### 5.3 What each alternative means for the repository's licence

| Alternative | Repository licence | Third-party code in the repository | Hackathon rule "an open-source licence shown" |
|---|---|---|---|
| A0, A1, A | MIT, unchanged | none | met as today |
| B | MIT for the host tools and documents; the agent GPL-2.0-or-later | LibVNCServer, copied in or fetched by tag and hash | met, with two licences to explain |
| C, with or without D | MIT, unchanged | none | met as today |

### 5.4 David's answers, 2026-11-15

The first draft put eight questions to David, Q1 to Q8. He answered them on 2026-11-15, before the review. Q9 to
Q15 are further decisions of his from the same day, taken while this document was revised. The rest of the document
is written to these answers.

| # | Short title | Question | David's answer |
|---|---|---|---|
| Q1 | C beside vncserv | Alternative C deployed as D? | Yes. The program runs beside `vncserv` (ours on port 5901, `vncserv` on port 5900) and never replaces or disables it. |
| Q2 | source only | Source only in the repository and its releases? | Yes: source only, never a built binary and never a target image. |
| Q3 | B kept open | Keep B open as a fallback? | No. B is not pursued. C only, with A as the fallback. |
| Q4 | start by hand | Start the agent by hand until the acceptance run, and at boot only later, with David's go? | Yes. |
| Q5 | the tool's name | What is the tool called, given that QNX has a utility named `on`? | One binary named `opticnerve`. The server is started as `opticnerve serve`. Status and control are subcommands of the same binary (section 4.10). The resource manager's path is `/dev/opticnerve`. The first draft's shorter names, built on the letters "on", are dropped: such a name "can be perceived as related to the 'on' util". |
| Q6 | who may use the agent | Who may use `/dev/opticnerve`: root, one group, or every user? | Any user: `status`, `wait`, `capture`, `keepawake` and `label`, with no group restriction. Root only: `serve`, `stop`, `passwd` and any reload of the VNC password file. Input stays through the RFB connection only, with the VNC password. Any local user can capture the screen without the VNC password; the quick-start image is a single-user test target. The server process runs as root, as `vncserv` is started on the quick-start image (section 4.11). |
| Q7 | keyboard and mouse on the Pi | A USB keyboard and mouse on the Pi for S11 (keyboard and mouse reference)? | Yes. Both have been attached the whole time. The keyboard is a Logitech Classic Keyboard 200, model Y-UR83, part number 868017-0403. The reference needs a stated list of keys; it is in section 4.9. |
| Q8 | build host baseline | Which SDP baseline does the build host use? | Settled: one of the levels QNX defines, for traceability. Baseline SDP-BL-006 in the folder `C:\Users\root\qnx805` is the SDP 8.0.5 release as QNX ships it, plus the packages named in section 4.13. |
| Q9 | SDP hosts | Which hosts hold an SDP installation? | The Windows laptop is the only SDP development host. The SDP installation on UBUNTU24 will be removed; UBUNTU24 stays a test client and a reviewer. A second SDP installation may later go on the HC8000 server when it is online. |
| Q10 | no judgment of QNX software | What may the repository say about QNX software? | "no judgment of the QNX 8 software or infrastructure (eg. myqnx or qnx software center) shall be stored on the repo. Instructions of what we used are permitted because it allows judges to reproduce our results." |
| Q11 | a second server for fault isolation | Which second server tells a server fault from a client fault? | TigerVNC on UBUNTU24. A test that fails against our server is repeated against it. It runs on Linux and so cannot stand in for the target's own input path (section 7). |
| Q12 | results about our own server | May results about our own server be public? | Yes, for example its frame rate and its processor time. |
| Q13 | capture speed | Does gate G1 (Design) set a number for capture speed? | No. The capture time of our own server is measured and reported. The target is set at gate G4 (Test plan), from our own measurements. The expected basis is that a capture must fit inside one step of a step-and-check test, about one second (section 4.6). |
| Q14 | the target's configuration | What is the target for the tests? | The quick-start image with its own `vncserv` 4.2.0 on port 5900, left running and untouched. The Pi is flashed again from the quick-start image before the acceptance runs for the submission (section 7). |
| Q15 | what the demo shows | What does the demo show, and what if the input path is not ready? | Only our own software, with what works at that point. If the input path is not ready by the deadline: view-only frames through our server and the agent's status features (frame freshness, display state, capture, labels). No demo, in any fallback, shows `vncserv` or depends on it visibly, and none shows two servers (section 8). |

Further questions about the QNX licence were put to David in the private review record R-001 and are answered
there, not here.

## 6. Risks and unknowns, each with the measurement that settles it

Where a result is recorded. S7 (input injection reaches an application), S8 (sustained capture and the dispatch
loop), S10 (label window placement), S11 (keyboard and mouse reference) and S12 (dropping root) measure QNX
interfaces, and so does the test behind U9 (unsignalled changes). Their results go to the private review record.
This document is then updated with what the agent uses: the call, the context type, the setting. S9 (OculiX accepts
Raw), S13 (encoding cost) and the tests of our own server are reported publicly.

| # | Short title | Unknown | Why it matters | Settled by | If the answer is bad |
|---|---|---|---|---|---|
| U1 | build host baseline | **Closed on 2026-11-15.** The build host is the Windows laptop with baseline SDP-BL-006 (section 4.13) | no build is allowed on a host that does not match its baseline | before each build, `python sdp/check_sdp.py` prints `MATCHES THE BASELINE` ([HOW_WE_WORK.md](HOW_WE_WORK.md)). It was not run for this document | no build until it matches |
| U2 | input injection | Does `screen_inject_event()` deliver keys and pointer events to applications, from an input provider context, while the image's window manager runs? | R6 (input through Screen); the reason for B or C over A | **S7 (input injection reaches an application)**: a throw-away probe injects a pointer move, a click and three keys, and the target test application logs what it receives. One visible slot on the Pi, with David's go | try devices of our own, then the `/dev/screen/.inject` route. If none works, our server is view-only (section 8) |
| U3 | capture rate and the loop | Does a whole-display read at 1920 x 1080 stay under the handler limit of section 4.2, and what share of the loop's time does capturing take at the interval of section 4.5? Does `select_attach()` do what the loop needs with sockets? | sections 4.2 and 4.5 | **S8 (sustained capture and the dispatch loop)**: 300 reads in a loop, timed; a small echo server on a dispatch loop | a longer capture interval, so fewer captures a second; or `ionotify()` used directly |
| U4 | Raw with OculiX | Does OculiX under ON-VNC-1 decode Raw at 32 bpp correctly? Does OculiX 4.0.0 alone? | section 4.6 | **S9 (OculiX accepts Raw)**: on a host, with no Pi: the fake server shows a known pattern in Raw; `ReadOnlyProbe` must find 0 wrong pixels | ZRLE moves from "if needed" to "first" |
| U5 | label placement | Can the label window be placed and kept above the launcher under the image's window manager? | section 4.10 | **S10 (label window placement)**: the probe's window with a position and a size; one capture | no label on screen; the desktop name and the status only |
| U6 | keyboard reference | What does the USB keyboard on the Pi (section 4.9) produce for the keys listed there, among them Delete, keypad Delete with NumLock off and on, the Super key, Alt, AltGr, Shift+letter, Ctrl+Shift+letter and Shift+Tab? Which way does a wheel step go? | section 4.9: the reference for the keymap | **S11 (keyboard and mouse reference)**: the target test application logs the events while David presses the keys | the table rests on documentation alone and says so |
| U7 | dropping root | Can the agent drop root after start-up and still capture and inject? | section 4.11 | **S12 (dropping root)**; not before the deadline | it stays root, as planned |
| U8 | encoding cost | What does ZRLE cost on the Pi? How long does an OculiX capture take from each test host with Raw and with ZRLE? | section 4.6 | **S13 (encoding cost)**: once the Raw path works; timings from both hosts | Raw only. The slower host then has slower captures, and the report says so |
| U9 | unsignalled changes | Does a change ever reach the screen without a composition signal, for example content on a hardware layer of its own? | section 4.5 | the counter `unsignalled_changes` over the whole acceptance run, with the image's video and OpenGL samples running: test FR04 (no unsignalled change in the whole run) | the safety capture becomes regular polling at a stated interval. A change can then reach a client up to one interval late, and the README states the interval as a setting of ours |
| U10 | two servers at once | Do our agent and `vncserv`, which then both read the display and can both inject input, disturb each other? | alternative D | every test of our agent runs with `vncserv` running beside it, as the image starts it (section 7); test CX02 (`vncserv` is left as found) | David decides. The design does not stop or change `vncserv` on its own |
| U11 | one update for two requests | Does OculiX accept one update in reply to its two back-to-back requests? | section 4.5 | test PX03 (an OculiX capture equals a raw-tool frame) | each request gets its own update |
| U12 | held button after a kill | What happens to a held button if the agent is killed? | section 4.12 | test RB06 (the agent is killed while a button is held) | the deploy script's stop step starts the agent once more to release it |
| U13 | gates in time | Gates G2 (Protocol), G3 (Concurrency) and G4 (Test plan) each need two reviewers and a disposition within four weeks | R14 (fits the time) | the dates of section 8 | David may waive a review (REVIEWS.md) |

## 7. Test outline for gate G4 (Test plan)

The suite keeps the form of [HOW_WE_WORK.md](HOW_WE_WORK.md): setup tests, tests, teardown tests; foundations first;
a skipped test names the prerequisite that failed. Every test below runs against our own agent on port 5901, or
against the fake server on a host. Results about our own server are public (section 5.4, Q12 (results about our own
server)). Where a test counts something our server did, for example how often the safety capture of section 4.5
found a change in FR04 (no unsignalled change in the whole run), that count is a result about our server and is
reported as such. This repository draws no conclusion from it about any other component.

**The target.** The quick-start image with its own `vncserv` 4.2.0 on port 5900, started as the image starts it and
left running and untouched, and our agent on port 5901 (section 5.4, Q14 (the target's configuration)). The image's
`vncserv` is the build of package `target.screen.vncserv` 4.2.0.00070, which baseline SDP-BL-006 holds as well
[doc: FIRSTBOOT; CONFIG]. Setup test SV05 (the target is in the stated configuration) checks this before a run.

**A precondition of the final runs.** Before the acceptance runs for the submission the Pi is flashed again from
the quick-start image, whose file and SHA-256 are in CONFIG, and our install is made again as a recorded target
change. The final runs then start from the image as it is shipped, plus what CONFIG records.

**A second server, to tell a server fault from a client fault.** A test that fails against our server is repeated
against a second, independent RFB server. If it fails there in the same way, the fault follows the client, and it
is looked for in OculiX or in the test. If it passes there, the fault is looked for in our server. The second
server is TigerVNC on UBUNTU24: `Xtigervnc` 1.13.1 on a private test display. The matrix already records OculiX
against it, read-only at depth 24 and at depth 16, and with input at depth 16 [measured: MATRIX, "Sources" and the
master table]. Its limit: it runs on Linux, with an X server behind it. It cannot stand in for the target's own
input path, which is Screen and the keymap of section 4.9, or for the agent's features. So a repeat there can clear
the client or point to it. It cannot show that our server's injection is right. The groups that can be repeated
there are Handshake, Pixels, Input and Guardrail. The display the matrix used listens on localhost only [measured:
MATRIX, "Sources"]. A repeat from the Windows host therefore needs it to listen on the LAN, which is a recorded
change on UBUNTU24; a repeat from UBUNTU24 itself changes the host as well as the server. Its set-up is to be
recorded in CONFIG before the first acceptance run, like the other servers; CONFIG has no row for it yet
[measured: CONFIG].

Two new sources of evidence. The **target test application** (ours) shows patches of known colours, among them values
that 16 bits cannot hold, and writes every input event it receives to a log. It replaces "what the terminal showed"
as the receiving side. The **fake server** is `core/` with a synthetic framebuffer, on a host.

| Group | Tests | Pass criterion | Runs against |
|---|---|---|---|
| Host tests, no Pi | HT01 (DES against published vectors and the Python tools); HT02 (pixel conversion against a reference written independently in Python); HT03 (fuzzing the core with random and mutated client bytes); HT04 (unit tests of the core: client table, request clipping, tile compare); HT05 (OculiX `ReadOnlyProbe` against the fake server, with OculiX alone, with 2.0.2 first and under ON-VNC-1) | exact equality; no crash and no sanitizer report in 10 million inputs; 0 wrong pixels | the fake server |
| Setup | the suite's existing setup tests, and new ones: SV01 (the agent is up, and `vncserv` is running as the image starts it); SV02 (versions read from the desktop name and the status); SV03 (core-dump baseline recorded); SV04 (the installed build's hashes equal those in CONFIG); SV05 (the target is in the stated configuration: the quick-start image and its own `vncserv` build, by the hashes CONFIG records) | all pass, or the run is skipped with the reason | the target |
| Handshake | HS01 (version exchange); HS02 (security types arrive in one segment); HS03 (right password); HS04 (wrong password: failure with a reason, connection closed, counted); HS05 (no password file: the VNC side does not start); HS06 (half-open handshake is closed); HS07 (unknown message type: that client closed, server up); HS08 (ServerInit: size, format, name) | as stated | our server |
| Pixels | PX01 (every patch of the test application has exactly its known value in a raw-tool frame); PX02 (16 bpp and byte-swapped formats convert exactly); PX03 (an OculiX capture equals a raw-tool frame); PX04 (a snapshot through `opticnerve capture` equals the same region of the VNC frame); PX05 (odd requests: empty, outside the screen, 65527 x 65527 at 10,10) | exact equality outside the label's rectangle | our server |
| Freshness | FR01 (static screen for 10 s: no update, composition count constant); FR02 (one patch changes: an update within 200 ms that contains it, content number up by one); FR03 (`opticnerve wait` returns on a change and times out without one); FR04 (no unsignalled change in the whole run); FR05 (an animated sample for 30 s: frame rate, the agent's processor time, no growing queue); FR06 (a full-screen OculiX capture from each test host: its time is measured and reported; gate G4 (Test plan) sets the threshold) | as stated | our server |
| Input | the Pi column of MATRIX, step by step, with the same step files through the tap and the receiving side from the test application's log: IN01 (pointer moves are counted); IN02 (click); IN03 (double click); IN04 (right click); IN05 (drag); IN06 (wheel); IN07 (typing ASCII); IN08 (special keys and modifiers); and IN09 (a held button is released when its client disconnects) | what was sent equals what was received; for the last of these tests the log shows the release and the status shows nothing held | our server |
| Keymap | one test for each row of the keymap table, numbered from KM01 (keypad Delete): the injected event against the keyboard reference | equal key cap, symbol and modifiers | our server |
| Guardrail | GD01 (`Key.ENTER` stays without effect: Linefeed reaches the server, nothing is injected, `keys_unmapped` goes up); GD02 (`Key.DELETE` from a Windows client types "."); GD03 (`paste` delivers only Ctrl+V); GD04 (a bare `mouseUp()` leaves the button held while connected). **These tests fail if our server makes an OculiX-only defect disappear** | the defect is still visible | our server |
| Agent | AG01 (status fields); AG02 (keep-awake counter 0, 1, 0); AG03 (label text and rectangle); AG04 (an ordinary user has status and the everyday controls: `qnxuser` runs `status`, `wait`, `capture`, `keepawake` and `label` without `sudo`); AG05 (a waiter is killed while waiting: the agent is unaffected); AG06 (an ordinary user cannot stop the agent or change its password: as `qnxuser`, `opticnerve stop` and `opticnerve passwd` are refused and so is a request to read the password file again; the agent keeps running, and the password in use still logs in); AG07 (no subcommand starts nothing: with no subcommand or an unknown one the program prints its usage, and `opticnerve serve` as `qnxuser` exits with an error and leaves no device and no socket) | as stated | our server |
| Robustness | RB01 (connect and disconnect in every state: 10,000 connections closed at a random point, with the AddressSanitizer build); RB02 (the client sequence of the Windows → Pi input run, 1,000 times); RB03 (a stalled client does not delay others: `opticnerve status` and a second client answer within 200 ms, and the stalled client is closed after 30 s); RB04 (memory and descriptors flat over a soak of 8 h); RB05 (Screen restarted under the agent: clients closed, the agent recovers); RB06 (the agent is killed while a button is held) | no sanitizer report, no core dump, the same process, and as stated | our server |
| Coexistence | CX02 (`vncserv` is left as found: it has the same process ID at teardown as at setup) | as stated | the target |
| Teardown | TD01 (target back to its initial condition), extended as in section 4.12 | the launcher frame equals the initial frame; no new core dump; nothing left running that setup did not find | the target |

Tests that must fail. Each is run once, to prove that the suite can see the fault (REVIEWS.md: "a passing test that
would have passed anyway proves nothing"):

| # | Fault planted in a test build or in the fake server | Test that must turn red |
|---|---|---|
| MU01 (split security message) | the count and the list are sent in two writes 50 ms apart | HS02 (security types arrive in one segment); and HT05 (OculiX `ReadOnlyProbe` against the fake server) times out with 2.0.4, which reproduces ON-OX-004 (2.0.4 loops on a split security-type list) under a time limit |
| MU02 (one wrong pixel) | one bit is flipped in one pixel of the Raw encoder | PX01 (every patch has its known value); PX03 (an OculiX capture equals a raw-tool frame) |
| MU03 (deaf to compositions) | composition pulses are ignored | FR02 (one patch changes); and FR04 (no unsignalled change in the whole run) reports the safety capture |
| MU04 (Linefeed becomes Return) | Linefeed is mapped to Return | GD01 (`Key.ENTER` stays without effect) |
| MU05 (double free) | `client_free()` is called twice for one client | RB01 (connect and disconnect in every state), under AddressSanitizer |
| MU06 (no release) | the release at disconnect is removed | IN09 (a held button is released when its client disconnects) |
| MU07 (no check for root) | the check for root on the stop request is removed | AG06 (an ordinary user cannot stop the agent or change its password) |

## 8. Milestones to 2026-12-15, 14:00 Eastern

The order follows the gates: gate G3 (Concurrency) before any client handling, gate G2 (Protocol) before input code,
gate G4 (Test plan) before the first acceptance run. Spike probes are throw-away programs like
`agent/spikes/screen_probe.c`, not server code. Each run on the Pi is a target change and needs David's go. S7
(input injection reaches an application) runs first precisely so that a risk to M3 (act) is known in the first
week and not in the last.

| # | Dates | Deliverable | Done when |
|---|---|---|---|
| M0 (design review closed) | 15 to 17 Nov | Gate G1 (Design): review R-001 and its disposition. Done on 15 Nov: David's answers (section 5.4) and the build host at a recorded baseline, SDP-BL-006 | the disposition is recorded |
| **M1 (spikes)** | 17 to 19 Nov | **The smallest first deliverable: S7 (input injection reaches an application)**, with the target test application in its first form: a window that logs the input events it receives. Beside it: S9 (OculiX accepts Raw) on a host; and in the same Pi slots S8 (sustained capture and the dispatch loop), S10 (label window placement) and S11 (keyboard and mouse reference) | a yes or a no for U2 (input injection), with the log |
| Gate G3 (Concurrency) | request 18 Nov, findings by 20 Nov | sections 4.2 and 4.3, with the code skeleton of the loop and the client table | the disposition is recorded |
| M2 (see) | 20 to 24 Nov | `core/` with the handshake, VncAuth and Raw; the loop; capture and compare; the status text. The host tests | `vnc_snapshot.py` and OculiX `ReadOnlyProbe` get correct frames from port 5901: the handshake and pixel groups pass for our server |
| Gate G2 (Protocol) | request 22 Nov, findings by 24 Nov | sections 4.7 to 4.9, with the keymap table filled in from S11 (keyboard and mouse reference) | the disposition is recorded |
| M3 (act) | 25 to 29 Nov | injection, the keymap, held keys and buttons, the release at disconnect | the input, keymap and guardrail groups pass for our server |
| M4 (agent) | 30 Nov to 3 Dec | waiters, snapshots, keep-awake, the label, the control subcommands of `opticnerve`, deploy and undo. ZRLE, if S13 (encoding cost) calls for it | the agent group passes; `opticnerve_deploy.py --undo` leaves the target as it was found |
| Gate G4 (Test plan) | request 1 Dec, findings by 3 Dec | section 7 written out as the test plan | the disposition is recorded |
| M5 (acceptance) | 4 to 8 Dec | the Pi flashed again from the quick-start image and our install made again (section 7); the acceptance run; the robustness and coexistence groups; the soak; the x86_64 build; fixes | every group has a result, and failures are listed as failures |
| M6 (submission) | 9 to 14 Dec | the demo application and suite; the README with build and run instructions; the video, which shows only our own software; the tutorial; gate G5 (Release) | submitted on 14 Dec, with one day in hand |

Where the plan falls back. In every case the demo shows only our own software, with what works at that point. No
demo shows `vncserv` or depends on it visibly, and none shows two servers (section 5.4, Q15 (what the demo shows)).

- After M1 (spikes), if S7 (input injection reaches an application) says no on every route: our server is
  view-only. It serves frames and freshness, and the agent's features stay. The input, keymap and guardrail groups
  are skipped, with that reason. The demo shows view-only frames through our server and the agent's status
  features.
- After M2 (see), if the read-only acceptance has not passed by 26 Nov: David decides between more time for C and
  alternative A. B is not a choice (section 5.4, Q3 (B kept open)). Under A the demo shows the agent's status
  features: frame freshness, display state, capture and the label.
- After M3 (act), if keys are not right by 1 Dec: the demo is prepared in its view-only form, which is frames
  through our server (M2 (see)) and the agent's status features: frame freshness, display state, capture and
  labels. Input joins the demo only if it is ready before the deadline.

## 9. What was not checked

- Nothing was run on the Pi for this document, and the Pi was not contacted. Every statement about the target comes
  from the notes named. Whether the Pi is at present in the configuration that section 7 states was not checked;
  setup test SV05 (the target is in the stated configuration) checks it before a run.
- Nothing was run on UBUNTU24 either. What is said about TigerVNC there is read from MATRIX.
- `sdp/check_sdp.py` was not run. What is said about baseline SDP-BL-006 is read from CONFIG and from `sdp/`. That a
  program built from it ran on the Pi 5 is CONFIG's statement and was not repeated. On the build folder itself only
  a file listing was made, for `qcc.exe`, `sys/keycodes.h`, `zlib.h`, `libz.so`, `libasan.so`, `libubsan.so`,
  `libscreen.so` for both targets and `bin/on`, and `sys/iofunc.h` was searched for one function's name. Nothing was
  built.
- The pages of the QNX documentation and the licence were read for the first draft, on 2026-11-15. They were not
  read again for the revision of the same day. The header `sys/keycodes.h` was looked at again, in the build folder.
- The revision removed statements under the rule of section 4.14. The notes this document still cites (MATRIX,
  ISSUES, FIRSTBOOT, CONFIG) are cited for results about OculiX and about servers that are not QNX's, and for how
  the target and the hosts are set up. Those notes come under the same rule and were revised on the same day, while
  this document was. Each citation here was checked against them as they stood after that revision. If a cited
  passage is taken out of one of them later, the citation here has to follow.
- The mouse on the Pi is not identified, and whether the keyboard has every key on the list of section 4.9 was not
  checked. Whether each test client has a C compiler for the host tests was not checked.
- The LibVNCServer source was not downloaded or read. Statements about its interfaces are the author's recollection
  and are marked as inference. Its issues were read by title only.
- The OculiX client source was not read. Its behaviour is taken from MATRIX, ISSUES and the tap log.
- The GPL text and the hackathon rules page were not read again.
- Of QNX's pages on input, only those listed under QNX-API and QNX-GUIDE were read; the pages on input sessions were
  not.
- That TigerVNC and x11vnc hold back incremental requests on a static screen, and that TigerVNC's server releases the
  keys of a client that disconnects, are the author's knowledge of those programs. We did not measure them.
- All line counts, durations and dates in sections 3 and 8 are estimates.
- Which library call supplies random bytes on QNX 8.0 was not settled. The design only requires the system's random
  source.
