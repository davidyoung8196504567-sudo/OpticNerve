# Draft: OculiX bug report (not filed yet)

Found by David on the Windows host on 2026-11-27 and confirmed from a capture of the IDE window the same day. As with
the other drafts here, the Oculix session drafts the report and David raises it together with a fix from his fork.
Existing issues not searched yet.

---

**Title:** `[Bug]: IDE title bar shows blank hide-to-tray, minimise and maximise/restore buttons; only Close has its icon`

**Summary**

In the OculiX IDE 5.0.0-qtb9 the title bar is drawn by OculiX itself. At its right end, three light-blue squares stand
where hide-to-tray, minimise and maximise/restore should be, with no icon in them. The red Close button shows its X. The blank
buttons work when clicked; only their icons are missing.

![Right end of the IDE title bar](oculix-qtb9-title-buttons.png)

**Where seen**

- OculiX IDE 5.0.0-qtb9 (`oculixide-5.0.0-qtb9-complete-win-sha256-a66002cf789a.jar`), started with
  `java -cp <jar> org.sikuli.ide.Sikulix`, Light theme.
- Windows 11 Pro (26300), display scaling 150 %, Temurin Java 25.0.4.
- Not seen on screen: Dark theme, 100 % scaling, Linux or macOS. The tile images are flat whatever the theme or scale,
  so they should show the same squares.

**Effect:** cosmetic, but a user cannot tell which square minimizes and which maximizes without trying them.

**Cause** (found by the Oculix-MacBookM4Pro-macos-27 session, 2026-11-27, inside the QTB-9 jar and on upstream master
be236b24): the tiles are drawn by `OculixWindowIcon` (upstream a5d410df) from PNG files. Upstream 6ba09a07
(2026-10-10) re-exported hide, iconify and maximize at 18 and 36 px with `_light` variants, and all 12 files came out
as one flat colour with no symbol. The earlier images (64fbbab3) had symbols. `close.png` was not re-exported and
keeps its X. OculiX 4.0.0 has neither these tiles nor `TitleBarIcons`, so it shows FlatLaf's own symbols and is not
affected. No QTB change touches the title bar icons. Oculix session draft 37.
