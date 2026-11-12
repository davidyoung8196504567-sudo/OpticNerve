# Draft: OculiX bug report (not filed yet)

Plan (David, 2026-11-12): David contributes fixes to OculiX through his fork
<https://github.com/davidyoung8196504567-sudo/Oculix>. OculiX problems found in this project are fixed there after the
ON agent work is done; until then OpticNerve uses the workaround below and stays on the pinned 4.0.0 jars.

For <https://github.com/oculix-org/Oculix/issues/new?template=bug_report.yml>. Fields follow the "Bug report" form.
Searched existing issues on 2026-11-12 ("VNCScreen", "ClassCastException DataBufferInt", "getMatList"): no duplicate.
Related only by area: #443 (ZRLE CPIXEL, closed), #481 (ZlibInStream).

---

**Title:** `[Bug]: VNCScreen find/exists throws ClassCastException (DataBufferInt -> DataBufferByte) in Commons.getMatList`

**Summary**

Any image search on a `VNCScreen` (`exists`, `find`, `wait`, `click(Pattern)`) fails with
`ClassCastException: DataBufferInt cannot be cast to DataBufferByte`, because the VNC capture is a `TYPE_CUSTOM`
image backed by a `DataBufferInt` and `Commons.makeMat` sends every `TYPE_CUSTOM` image to `getMatList`, which
assumes a byte buffer.

**Steps to reproduce**

1. Run any RFB 3.8 VNC server with VNC password authentication (seen with QNX 8.0's `vncserv` on a Raspberry Pi 5,
   1920 x 1080; the default pixel format there is rgb565).
2. Connect: `VNCScreen screen = VNCScreen.start(host, 5900, password, 10, 1000);` (connects and captures fine).
3. Call `screen.exists(new Pattern("any.png").similar(0.8), 0);`
4. Observe the exception below.

**Expected behavior**

The pattern is searched in the VNC screen image, as on a local `Screen`.

**Actual behavior**

```
java.lang.ClassCastException: class java.awt.image.DataBufferInt cannot be cast to class java.awt.image.DataBufferByte
	at org.sikuli.support.Commons.getMatList(Commons.java:2541)
	at org.sikuli.support.Commons.makeMat(Commons.java:2596)
	at org.sikuli.support.Commons.makeMat(Commons.java:2558)
	at org.sikuli.script.Finder.setScreenImage(Finder.java:94)
	at org.sikuli.script.Finder.initScreenFinder(Finder.java:88)
	at org.sikuli.script.Finder.<init>(Finder.java:84)
	at org.sikuli.script.Region.doCheckLastSeenAndCreateFinder(Region.java:3144)
	at org.sikuli.script.Region.checkLastSeenAndCreateFinder(Region.java:3050)
	at org.sikuli.script.Region.doFind(Region.java:2985)
	at org.sikuli.script.Region$RepeatableFind.run(Region.java:3296)
	at org.sikuli.script.Region$Repeatable.repeat(Region.java:3241)
	at org.sikuli.script.Region.doExists(Region.java:2491)
	at org.sikuli.script.Region.exists(Region.java:2456)
```

`screen.capture().getImage().getType()` returns `0` (`TYPE_CUSTOM`).

**Minimal reproducer** (Java, run with `java -cp oculixapi-4.0.0-windows.jar Repro.java HOST PASSWORD any.png`)

```java
import org.sikuli.script.Pattern;
import org.sikuli.vnc.VNCScreen;

public class Repro {
    public static void main(String[] a) throws Exception {
        VNCScreen screen = VNCScreen.start(a[0], 5900, a[1], 10, 1000);
        System.out.println("image type " + screen.capture().getImage().getType()); // 0 = TYPE_CUSTOM
        System.out.println(screen.exists(new Pattern(a[2]).similar(0.8), 0));        // ClassCastException
        screen.stop();
        System.exit(0);
    }
}
```

**Operating system:** Windows 11

**Java version:** `openjdk version "25.0.4" 2026-07-21 LTS` (Temurin-25.0.4+7)

**OculiX version / artifact:** `oculixapi-4.0.0-windows.jar` (SHA-256
`b8ff76a74e582c7d36df2ca1dfb3f09bf564395d47502a9da5db84a304f70638`, the asset as re-uploaded on 2026-08-26)

**Where does the bug happen?** API (Screen / Region / Pattern / Match); VNC stack

**Additional context**

- Cause, in `API/src/main/java/org/sikuli/support/Commons.java` (same code at `v4.0.0`, `v4.1.0-rc1` and `master`
  954591b, 2026-11-05): `makeMat(BufferedImage, boolean)` routes `TYPE_CUSTOM` together with `TYPE_4BYTE_ABGR` to
  `getMatList`, whose first line is `((DataBufferByte) bImg.getRaster().getDataBuffer()).getData()`. The VNC frame
  buffer image is `TYPE_CUSTOM` with an int buffer.
- Possible fix: in `makeMat`, handle `TYPE_CUSTOM` (or any image whose data buffer is not a `DataBufferByte`) by
  drawing it into a `TYPE_3BYTE_BGR` image first, as the `TYPE_BYTE_INDEXED` / `TYPE_BYTE_BINARY` branch already does.
- Workaround that works for us: copy `screen.capture().getImage()` into a `TYPE_3BYTE_BGR` `BufferedImage`, run
  `new Finder(copy).find(pattern)`, and click the match's target through the `VNCScreen`. The match and the click
  both work (found at score 1.0 in about 0.1 s on 1920 x 1080; the click reached the target).
- Context: we use OculiX to test QNX 8.0 HMIs over QNX's own `vncserv` (project OpticNerve,
  <https://github.com/davidyoung8196504567-sudo/OpticNerve>).
