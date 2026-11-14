# Draft: OculiX bug report (not filed yet)

Found by the ON-UBUNTU24 session on 2026-11-14. Same plan as
[oculix-vncscreen-find-classcast.md](oculix-vncscreen-find-classcast.md): David raises it together with a fix from
his fork after the ON agent work. Not seen against QNX's `vncserv`. Existing issues not searched yet for this one
(#443, ZRLE CPIXEL, closed, is in the same area).

---

**Title:** `[Bug]: VNCScreen: ZRLEDecoder assertion kills the reader thread against TigerVNC 1.13.1; captures are silently wrong`

**Summary**

Against TigerVNC's `Xtigervnc` 1.13.1, the VNC reader thread dies during `VNCScreen.start` with a ZRLE decoder
assertion. `start` still returns a running screen and every later call reports success, but the framebuffer is
wrong wherever the server sent ZRLE rectangles: windows show as black and white blocks.

**Steps to reproduce**

1. Ubuntu 24.04, `Xtigervnc` 1.13.1+dfsg-2build2, display 1920 x 1080, depth 24 (32 bits per pixel), VncAuth.
   Content: solid root window, an `xmessage` window with 3 lines of text, `xeyes`.
2. `VNCScreen screen = VNCScreen.start(host, port, password, 10, 1000);` with `oculixapi-4.0.0-linux.jar`,
   Temurin 25.0.4+7.
3. Observe on stderr:

```
Exception in thread "Thread-0" com.tigervnc.rfb.Exception: ZRLEDecoder: assertion (len <= end - ptr) failed
  at com.tigervnc.rfb.ZRLEDecoder.readRect(ZRLEDecoder.java:135)
  at com.tigervnc.rfb.CMsgReader.readRect(CMsgReader.java:110)
  at com.tigervnc.rfb.CMsgReaderV3.readMsg(CMsgReaderV3.java:91)
  at com.tigervnc.rfb.CConnection.processMsg(CConnection.java:67)
  at com.sikulix.vnc.VNCClient.processMessages(VNCClient.java:246)
```

4. `screen.capture()` returns without error. Compared with a raw-encoding frame of the same display, 72,666 pixels
   differ, all inside the two windows (bounding box 101,40 to 1301,712); the solid background is right.

Repeated 3 times with the same result. The server log shows ZRLE rectangles of the Bitmap RLE, Indexed RLE and
Full Colour kinds, plus Solid ones.

**Expected**

The ZRLE rectangles decode correctly; or, if decoding fails, `start` or the next `capture` reports the error
instead of returning a stale or corrupt frame.

**Not checked yet**

- Whether later captures stay stale (the reader thread is dead, so they likely do).
- Whether other servers (x11vnc, wayvnc) show it, and whether limiting the encodings to raw avoids it.
