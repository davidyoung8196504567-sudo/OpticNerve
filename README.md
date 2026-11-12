# OpticNerve

**Gives your test tools eyes and hands on any QNX 8.0 screen.**

> Status: work in progress for the QNX Global Hackathon (November to December 2026). Nothing here builds yet.

## The problem

The HMI on a car dashboard, an infusion pump or a factory panel is tested by hand far more often than it should be.
Desktop GUI test tools can't see inside an embedded target: they need a JVM, a desktop and an input API that a
real-time OS doesn't provide. So teams either tap through test plans manually or build one-off rigs per product.

## The idea

Oculix (the open-source successor of SikuliX) already automates GUIs by **seeing** the screen and **acting** on it
with image matching and OCR. It runs on Windows, macOS and Linux. OpticNerve is the missing link to the target: the
**ON agent**, which puts eyeballs on a QNX 8.0 screen for Oculix 4.x on the host.

```
  host (Windows / macOS / Linux)                     target (QNX 8.0, e.g. Raspberry Pi 5)
 +------------------------------+                   +-------------------------------------+
 |  Oculix 4.x test scripts     |   VNC (RFB)       |  ON agent (OpticNerve)              |
 |  image match, OCR, asserts   | <---------------> |   - captures the display (Screen)   |
 +------------------------------+   frames out,     |   - injects touch / mouse / keys    |
                                    input in        |   - resource manager: /dev/opticnerve |
                                                    |  the app under test (any toolkit)   |
                                                    +-------------------------------------+
```

- **No Java on the target.** The agent is a small native C program; the heavy lifting stays on the host.
- **Toolkit-agnostic.** It works at the QNX Screen level, so it can test an app built with any GUI toolkit.
- **QNX-native.** Built as a resource manager using QNX message passing.

## Layout

| Folder | Contents |
|---|---|
| `agent/` | The QNX 8.0 agent (C): Screen capture, input injection, VNC server, resource manager |
| `host/` | Oculix scripts, a sample test suite and the connection preset |
| `image/` | Build files for Raspberry Pi 5 and QEMU boot images that include the agent |
| `docs/` | Plan, design notes and the tutorial |

## Build and run

To come: QNX SDP version, target (Raspberry Pi 5 hardware or QEMU) and toolchain will be listed here.

## Licence

MIT. See [LICENSE](LICENSE).
