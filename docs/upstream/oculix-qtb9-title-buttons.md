# Draft: OculiX bug report (not filed yet)

Found by David on the Windows host on 2026-11-27 and confirmed from a capture of the IDE window the same day. As with
the other drafts here, the Oculix session drafts the report and David raises it together with a fix from his fork.
Existing issues not searched yet.

---

**Title:** `[Bug]: IDE title bar shows blank minimize, maximize and restore buttons; only Close has its icon`

**Summary**

In the OculiX IDE 5.0.0-qtb9 the title bar is drawn by OculiX itself. At its right end, three light-blue squares stand
where minimize, maximize and restore should be, with no icon in them. The red Close button shows its X. The blank
buttons work when clicked; only their icons are missing.

![Right end of the IDE title bar](oculix-qtb9-title-buttons.png)

**Where seen**

- OculiX IDE 5.0.0-qtb9 (`oculixide-5.0.0-qtb9-complete-win-sha256-a66002cf789a.jar`), started with
  `java -cp <jar> org.sikuli.ide.Sikulix`, Light theme.
- Windows 11 Pro (26300), display scaling 150 %, Temurin Java 25.0.4.
- Not yet checked: Dark theme, 100 % scaling, Linux or macOS.

**Effect:** cosmetic, but a user cannot tell which square minimizes and which maximizes without trying them.
