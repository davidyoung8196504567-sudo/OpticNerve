# Draft: OculiX bug report (not filed yet)

Found by the ON-UBUNTU24 session on 2026-11-14 and confirmed from the Windows host the same day. Same plan as
[oculix-vncscreen-find-classcast.md](oculix-vncscreen-find-classcast.md): David raises it together with a fix from
his fork after the ON agent work. Existing issues not searched yet.

---

**Title:** `[Bug]: VNCScreen decodes 16 bpp rgb565 framebuffers with wrong green and blue`

**Summary**

When the VNC server's pixel format is 16 bpp rgb565, the colours in `VNCScreen.capture()` (and so in
`Location.getColor`) are wrong. Red is right, black and white are right, green and blue are wrong.

**Steps to reproduce**

1. Run `Xtigervnc` 1.13.1 with `-depth 16` (server log: "depth 16 (16bpp) little-endian rgb565"), root window
   colour `#204060`. QNX 8.0's `vncserv` (default rgb565) shows the same.
2. `VNCScreen.start(host, port, password, 10, 1000).capture().getImage()`.
3. Compare with a raw-encoding frame of the same screen from another client, which asks the server for 32 bpp true
   colour.

**Measured**

| Raw-encoding frame | OculiX capture | Where |
|---|---|---|
| (32, 64, 98) | (33, 66, 49) | TigerVNC, background, 1,756,618 pixels |
| (246, 242, 246) | (247, 247, 123) | TigerVNC |
| (0, 4, 0) | (0, 0, 132) | TigerVNC |
| (49, 4, 0) | (49, 0, 132) | QNX `vncserv`, 1,470 pixels |
| (230, 231, 230) | (230, 230, 247) | QNX `vncserv`, 2,185 pixels |
| (49, 125, 197) | (49, 123, 230) | QNX `vncserv`, 1,668 pixels |
| (222, 194, 123) | (222, 197, 58) | QNX `vncserv`, 1,250 pixels |

On the Pi, 75,919 of 2,073,600 pixels differ. Two raw frames, one before and one after the OculiX capture, were
identical, so the screen did not change. The same count was measured from Ubuntu and from Windows. The reference
tool is trusted because at 32 bpp it returns exactly (32, 64, 96) for `#204060`.

**Likely cause** (from the numbers, not from the source): the client unpacks the 16-bit pixel with the wrong
field widths or shifts for green and blue (the lowest green bit seems to land in blue). Not proved.

**Effect:** `getColor` is wrong on such servers, and an image pattern taken from a true-colour screenshot scores
lower or is missed. Patterns cut from OculiX's own captures still match. OCR is hardly affected.
