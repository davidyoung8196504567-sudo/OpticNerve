// The Widget Factory Obstacle Course: operates the controls of GTK 4's Widget Factory (gtk4-widget-factory) on the
// QNX 8.0 quick-start image's Developer Desktop, driven by OculiX through the OpticNerve agent, and times each one.
// Each obstacle acts on a control and checks the screen: its region must change (and settle); where the action can
// be undone, the test undoes it and the region must be back to exactly what it was, so a run leaves the page as it
// found it. Values are read with OCR. A disabled control must not change when clicked. Prints PASS or FAIL per
// obstacle with its time, then a scoreboard; the exit status is the number of failures.
//
//   java -cp "tigervnc-java-oculix-2.0.4.jar;oculixapi-4.0.0-windows.jar" host/demo/ObstacleCourse.java HOST|- VNCPASSWORD|- [-cols=N]
// A - for HOST or PASSWORD takes ON_HOST or ON_VNC_PASSWORD from the environment or the settings file
// (host/tools/opticnerve.env.example); SSH goes to ON_SSH_HOST:ON_SSH_PORT (default HOST, port 22).
//
// Needs the agent running on HOST (port ON_VNC_PORT), the Developer Desktop on the screen and ON_LOGIN_PASSWORD set:
// the course closes Widget Factory (over SSH) and opens it again from the Applications menu, so that every run starts
// alike.
// All three pages. -only=A|B runs just the obstacles whose name contains A or B; -page=N just that page; -repeat=N
// each obstacle N times; -move=S OculiX's MoveMouseDelay (default 0.1 s).
import java.awt.image.BufferedImage;
import java.util.ArrayList;
import java.util.List;

import org.sikuli.script.Key;
import org.sikuli.script.KeyModifier;
import org.sikuli.vnc.VNCScreen;

public class ObstacleCourse {
    // A setting (docs/SETTINGS.md): the environment first, then the settings file ON_CONFIG (default
    // ~/.config/opticnerve/env), then the default given. The file's grammar is host/tools/on_env.sh's: a line
    // [export] NAME=VALUE [# comment], VALUE 'text', "text" or text without spaces, # or quotes, taken literally.
    static String cfg(String name, String dflt) {
        String v = System.getenv(name);
        if (v != null && !v.isEmpty()) return v;
        String f = System.getenv("ON_CONFIG");
        java.nio.file.Path p = java.nio.file.Paths.get(f != null && !f.isEmpty() ? f
                : System.getProperty("user.home") + "/.config/opticnerve/env");
        java.util.regex.Pattern line = java.util.regex.Pattern.compile("^\\s*(?:export\\s+)?" + name
                + "=(?:'([^']*)'|\"([^\"]*)\"|([^\\s#'\"]*))\\s*(?:#.*)?$");
        try {
            for (String l : java.nio.file.Files.readAllLines(p)) {
                java.util.regex.Matcher m = line.matcher(l);
                if (!m.matches()) continue;
                String x = m.group(1) != null ? m.group(1) : m.group(2) != null ? m.group(2) : m.group(3);
                if (!x.isEmpty()) return x;
            }
        } catch (java.io.IOException e) { /* no settings file, or not readable: the default */ }
        return dflt;
    }

    // HOST and PASSWORD on the command line, or "-" for the settings ON_HOST and ON_VNC_PASSWORD; the port is ON_VNC_PORT.
    static String target(String arg) { return arg.equals("-") ? cfg("ON_HOST", "10.30.1.116") : arg; }
    static String vncPassword(String arg) {
        if (!arg.equals("-")) return arg;
        String pw = cfg("ON_VNC_PASSWORD", null);
        if (pw == null) throw new IllegalStateException("set ON_VNC_PASSWORD (in the environment or the settings file)");
        return pw;
    }
    // host/tools/pi_ssh.sh with COMMANDS; SSH goes to ON_SSH_HOST, or else to the HOST given on the command line
    static String sshFallback;
    static ProcessBuilder pi_ssh(String commands) {
        ProcessBuilder pb = new ProcessBuilder("bash", "host/tools/pi_ssh.sh", commands).redirectErrorStream(true);
        if (sshFallback != null && cfg("ON_SSH_HOST", null) == null) pb.environment().put("ON_SSH_HOST", sshFallback);
        return pb;
    }
    static int vncPort() { return Integer.parseInt(cfg("ON_VNC_PORT", "5901")); }

    static VNCScreen s;
    static int cols;

    static void say(String prefix, String text) {
        if (cols <= prefix.length() + 10) {
            System.out.println(prefix + text);
            return;
        }
        String indent = " ".repeat(prefix.length());
        StringBuilder line = new StringBuilder(prefix);
        for (String w : text.split(" ")) {
            if (line.length() > prefix.length() && line.length() + 1 + w.length() > cols) {
                System.out.println(line);
                line = new StringBuilder(indent);
            }
            if (line.length() > prefix.length() && line.charAt(line.length() - 1) != ' ') line.append(' ');
            line.append(w);
        }
        System.out.println(line);
    }

    // With ON_CAPTION_FILE set, each caption also goes to that file, for a video's text overlay (OBS: a Text source
    // that reads from the file). Written beside it and moved in, so the overlay never shows a half-written line.
    static void overlay(String text) {
        String f = System.getenv("ON_CAPTION_FILE");
        if (f == null) return;
        try {
            java.nio.file.Path p = java.nio.file.Path.of(f), t = java.nio.file.Path.of(f + ".tmp");
            java.nio.file.Files.writeString(t, text);
            java.nio.file.Files.move(t, p, java.nio.file.StandardCopyOption.REPLACE_EXISTING,
                    java.nio.file.StandardCopyOption.ATOMIC_MOVE);
        } catch (java.io.IOException e) {
            System.err.println("caption file: " + e);
        }
    }

    static void caption(String text) {
        overlay(text);
        System.out.println();
        say("==> ", text);
    }

    // An obstacle failed: what was seen.
    static class Fail extends Exception {
        Fail(String what) {
            super(what);
        }
    }

    interface Run {
        String run() throws Exception;
    }

    // The scoreboard.
    static class Score {
        String page, name, result, detail, kind;
        double secs;
    }

    // What kind of obstacle the next one is: operated (the control does something), refused (a disabled control
    // must not respond) or none (no response is expected: the real mouse gets none either, or GTK draws it so). Set
    // by the obstacle's builder, read by obstacle().
    static String nextKind = "operated";

    static List<Score> board = new ArrayList<>();
    static String page = "";

    static String only; // -only=TEXT: just the obstacles whose name contains TEXT

    // Behaviour the real mouse shows too (David checked these on 2026-11-17).
    static final String REAL = "no visible change, as with the real mouse";

    static int repeat = 1; // -repeat=N: each obstacle N times in a row
    static String onlyPage; // -page=N: just that page

    static boolean selected(String name) {
        if (only == null) return true;
        for (String o : only.split("\\|"))
            if (name.contains(o)) return true;
        return false;
    }

    static void obstacle(String name, Run r) {
        String kind = nextKind;
        if (!selected(name)) {
            nextKind = "operated";
            return;
        }
        for (int i = 0; i < repeat; i++) {
            nextKind = kind;
            obstacleOnce(name, r);
        }
    }

    static void obstacleOnce(String name, Run r) {
        Score sc = new Score();
        sc.page = page;
        sc.name = name;
        sc.kind = nextKind;
        nextKind = "operated";
        long t0 = System.nanoTime();
        try {
            sc.detail = r.run();
            sc.result = "PASS";
        } catch (Fail f) {
            sc.result = "FAIL";
            sc.detail = f.getMessage();
        } catch (NumberFormatException e) { // OCR read something that is not a number: a failed check
            sc.result = "FAIL";
            sc.detail = "OCR read no number: " + e.getMessage();
        } catch (InterruptedException | RuntimeException | Error e) {
            // Not a check that failed but something broken (a NullPointerException in OculiX or here, ...): the course
            // stops, so that the cause is looked at before later obstacles fail because of it.
            sc.result = "FAIL";
            sc.detail = "error: " + e;
            board.add(sc);
            say("FAIL  ", name);
            say("            ", sc.detail);
            for (StackTraceElement st : java.util.Arrays.copyOf(e.getStackTrace(), Math.min(8, e.getStackTrace().length)))
                say("              at ", st.toString());
            throw new IllegalStateException("stopped on an unexpected " + e.getClass().getSimpleName() + " in \"" + name + "\"", e);
        } catch (Throwable e) {
            sc.result = "FAIL";
            sc.detail = "error: " + e;
        }
        sc.secs = (System.nanoTime() - t0) / 1e9;
        board.add(sc);
        if (sc.result.equals("FAIL")) // what the screen showed, for the failure; then Escape, so that a pop-up or a
            try { //                                         dialog left open does not take the next obstacle's clicks
                java.io.File f = new java.io.File("demo-out", String.format("course_fail_%02d.png", board.size()));
                f.getParentFile().mkdirs();
                javax.imageio.ImageIO.write(s.capture().getImage(), "png", f);
                sc.detail += "; the screen is in " + f.getPath();
                s.type(Key.ESC);
                s.type(Key.ESC);
                Thread.sleep(500);
            } catch (Throwable ignored) {
            }
        say(String.format("%s %5.1fs ", sc.result, sc.secs), name);
        if (sc.detail != null && !sc.detail.isEmpty()) say("            ", sc.detail);
    }

    static void skip(String name, String why) {
        if (!selected(name)) return;
        Score sc = new Score();
        sc.page = page;
        sc.name = name;
        sc.result = "SKIP";
        sc.detail = why;
        board.add(sc);
        say("SKIP        ", name);
        say("            ", why);
    }

    // --- The screen

    static BufferedImage grab(int[] r) {
        return s.capture(r[0], r[1], r[2], r[3]).getImage();
    }

    static boolean same(BufferedImage a, BufferedImage b) {
        if (a.getWidth() != b.getWidth() || a.getHeight() != b.getHeight()) return false;
        for (int y = 0; y < a.getHeight(); y++)
            for (int x = 0; x < a.getWidth(); x++)
                if (a.getRGB(x, y) != b.getRGB(x, y)) return false;
        return true;
    }

    // Waits until the region differs from `from` (or, with back, is the same as it) and then stays so for 200 ms;
    // returns whether it did within secs.
    static boolean await(int[] r, BufferedImage from, boolean back, double secs) throws Exception {
        long end = System.nanoTime() + (long) (secs * 1e9);
        do {
            BufferedImage now = grab(r);
            if (same(now, from) == back) {
                Thread.sleep(200);
                BufferedImage again = grab(r);
                if (same(again, from) == back && same(again, now)) return true;
            }
            Thread.sleep(80);
        } while (System.nanoTime() < end);
        return false;
    }

    static String ocr(int[] r) {
        return s.newRegion(r[0], r[1], r[2], r[3]).text().replaceAll("\\s+", " ").trim();
    }

    static String waitText(int[] r, String want, double secs) throws Exception {
        long end = System.nanoTime() + (long) (secs * 1e9);
        String t;
        do {
            t = ocr(r);
            if (t.contains(want)) return t;
            Thread.sleep(150);
        } while (System.nanoTime() < end);
        return t;
    }

    static int[] lastAt = { -1, -1 };
    static long lastClick;

    // Clicks; two clicks on one spot within GTK's double-click time would count as a double-click.
    static void click(int x, int y) throws Exception {
        if (Math.abs(x - lastAt[0]) < 8 && Math.abs(y - lastAt[1]) < 8) {
            long wait = lastClick + 800 - System.currentTimeMillis();
            if (wait > 0) Thread.sleep(wait);
        }
        press(x, y);
        lastAt = new int[] { x, y };
        lastClick = System.currentTimeMillis();
    }

    // OculiX's click, holding the button 50 ms as a hand does (ClickDelay; OculiX resets it after each click): a GTK
    // switch missed clicks whose press and release came together (2026-11-17). OculiX's mouseDown and mouseUp with
    // a pause between produced no clicks here.
    static void press(int x, int y) throws Exception {
        org.sikuli.basics.Settings.ClickDelay = 0.05;
        s.click(s.newLocation(x, y));
    }

    // A spot that does nothing when clicked but makes the window active: the header bar, beside the page tabs. (A
    // click on the page itself, on a label or empty space, did not always bring the window back from inactive.)
    static int[] neutral = { 1300, 46 };

    // Clicks the neutral spot: Widget Factory is drawn active again, and has the focus, after a dialog, a pop-up or
    // the web browser was in front of it.
    static void refocus() throws Exception {
        click(neutral[0], neutral[1]);
        away();
        Thread.sleep(300);
    }

    static void away() throws Exception {
        s.mouseMove(s.newLocation(1900, 1050)); // off the controls, so that none is drawn as under the pointer
    }

    static int[] box(int cx, int cy, int w, int h) {
        return new int[] { cx - w / 2, cy - h / 2, w, h };
    }

    // --- Kinds of obstacle

    // A click changes the region; a second click puts it back exactly. The region is the inside of the control: once
    // keys have been typed, GTK draws a focus ring around the control clicked last.
    static Run toggle(int x, int y, int[] r) {
        return () -> {
            away();
            BufferedImage before = grab(r);
            click(x, y);
            away();
            if (!await(r, before, false, 5)) throw new Fail("the click changed nothing");
            click(x, y);
            away();
            if (!await(r, before, true, 5)) throw new Fail("a second click did not put it back");
            return "changed, and back";
        };
    }

    // A click opens something (a pop-up, a dialog) over the region; Escape closes it and the region is as before.
    static Run opens(int x, int y, int[] r, String what) {
        return () -> {
            away();
            BufferedImage before = grab(r);
            click(x, y);
            if (!await(r, before, false, 6)) throw new Fail("no " + what + " opened");
            s.type(Key.ESC);
            Thread.sleep(500);
            refocus();
            if (!await(r, before, true, 3)) throw new Fail("the " + what + " did not close with Escape");
            return what + " opened, and closed with Escape";
        };
    }

    // A click opens a dialog. A dialog does not always open in the same place, and OCR missed its small title over
    // the page now and then: it is found by the sample points it changes (a large area), and it has closed when
    // nearly all of those points are back as they were (elsewhere the page may change meanwhile: a spinner turns).
    static Run dialog(int x, int y, String what) {
        return dialog(x, y, what, 4000);
    }

    // The same for something smaller than a dialog (a menu, a pop-up): at least `least` sample points change.
    static Run dialog(int x, int y, String what, int least) {
        return () -> {
            away();
            BufferedImage before = s.capture().getImage();
            click(x, y);
            away();
            boolean[][] area = null;
            int opened = 0;
            for (long end = System.nanoTime() + 8_000_000_000L; opened < least && System.nanoTime() < end; Thread.sleep(200)) {
                Thread.sleep(300); // the dialog drawn in full
                area = changedPoints(before, s.capture().getImage());
                opened = count(area);
            }
            if (opened < least) throw new Fail("no " + what + " opened (" + opened + " sample points changed)");
            s.type(Key.ESC);
            Thread.sleep(800);
            refocus(); // the window stays drawn inactive after its dialog closes, until it is clicked
            int left = opened;
            for (long end = System.nanoTime() + 4_000_000_000L; left > opened / 10 && System.nanoTime() < end; Thread.sleep(200)) {
                boolean[][] now = changedPoints(before, s.capture().getImage());
                left = 0;
                for (int i = 0; i < now.length; i++)
                    for (int j = 0; j < now[i].length; j++)
                        if (now[i][j] && area[i][j]) left++;
            }
            if (left > opened / 10) throw new Fail("the " + what + " did not close with Escape (" + left + " of its " + opened + " sample points still changed)");
            return what + " opened (" + opened + " sample points), and closed with Escape";
        };
    }

    // Sample points (every 4th pixel each way, below the desktop's top panel) that differ between two screens.
    static boolean[][] changedPoints(BufferedImage a, BufferedImage b) {
        boolean[][] m = new boolean[(1040 - 30) / 4][a.getWidth() / 4];
        for (int i = 0; i < m.length; i++)
            for (int j = 0; j < m[i].length; j++)
                m[i][j] = a.getRGB(j * 4, 30 + i * 4) != b.getRGB(j * 4, 30 + i * 4);
        return m;
    }

    static int count(boolean[][] m) {
        int n = 0;
        for (boolean[] row : m)
            for (boolean b : row) if (b) n++;
        return n;
    }

    // A disabled control: a click changes nothing.
    static Run disabled(int x, int y, int[] r) {
        nextKind = "refused";
        return () -> {
            away();
            BufferedImage before = grab(r);
            click(x, y);
            away();
            if (await(r, before, false, 1)) throw new Fail("it changed: it is not disabled");
            return "no change, as a disabled control";
        };
    }

    // Clicks tabs in turn; each changes the region; the first tab again puts it back.
    static Run tabs(int[] r, int[][] at) {
        return () -> {
            away();
            BufferedImage before = grab(r);
            for (int i = 1; i < at.length; i++) {
                BufferedImage was = grab(r);
                click(at[i][0], at[i][1]);
                away();
                if (!await(r, was, false, 2)) throw new Fail("tab " + (i + 1) + " showed nothing new");
            }
            click(at[0][0], at[0][1]);
            away();
            if (!await(r, before, true, 2)) throw new Fail("the first tab did not bring back the first page");
            return at.length + " tabs, and back to the first";
        };
    }

    // Types into an entry and reads it back; with clear, the entry's own clear button empties it.
    static Run entry(int x, int y, int[] r, String text, int[] clear) {
        return () -> {
            selectAll(x, y);
            typeSlowly(text);
            String got = waitText(r, text, 3);
            if (!got.contains(text)) throw new Fail("OCR read \"" + got + "\", not \"" + text + "\"");
            if (clear == null) return "OCR read \"" + got + "\"";
            click(clear[0], clear[1]);
            String after = ocr(r);
            for (long end = System.nanoTime() + 2_000_000_000L; after.contains(text) && System.nanoTime() < end; )
                after = ocr(r);
            if (after.contains(text)) throw new Fail("the clear button left \"" + after + "\"");
            return "typed and read back \"" + text + "\", and cleared";
        };
    }

    // One character at a time: typed at full speed, characters arrived out of order in a Developer Desktop
    // application (2026-11-17, in the web browser).
    static void typeSlowly(String text) throws Exception {
        for (char c : text.toCharArray()) {
            s.type(String.valueOf(c));
            Thread.sleep(40);
        }
    }

    // Drags a slider's knob; the region changes; dragging back changes it again.
    static Run slider(int x1, int y1, int x2, int y2, int[] r) {
        return () -> {
            away();
            BufferedImage before = grab(r);
            s.dragDrop(s.newLocation(x1, y1), s.newLocation(x2, y2));
            away();
            if (!await(r, before, false, 2)) throw new Fail("the drag did not move it");
            BufferedImage moved = grab(r);
            s.dragDrop(s.newLocation(x2, y2), s.newLocation(x1, y1));
            away();
            if (!await(r, moved, false, 2)) throw new Fail("dragging back did not move it");
            return "dragged, and back";
        };
    }

    public static void main(String[] a) {
        int code = 0;
        try {
            run(a);
        } catch (Throwable e) {
            say("FAIL ", "the course stopped on an error: " + e);
            code = 1;
        }
        try {
            if (s != null) s.stop();
        } catch (Throwable ignored) {
        }
        for (Score sc : board) if (sc.result.equals("FAIL")) code++;
        System.out.println();
        System.out.println(code == 0 ? "ALL PASSED" : code + " FAILED");
        System.exit(code);
    }

    static void run(String[] a) throws Exception {
        for (String x : a) if (x.startsWith("-cols=")) cols = Integer.parseInt(x.substring(6));
        for (String x : a) if (x.startsWith("-only=")) only = x.substring(6);
        for (String x : a) if (x.startsWith("-repeat=")) repeat = Integer.parseInt(x.substring(8));
        for (String x : a) if (x.startsWith("-page=")) onlyPage = x.substring(6);
        Object p = Class.forName("com.tigervnc.rfb.SecurityClient").getField("secTypes").get(null);
        p.getClass().getMethod("setParam", String.class).invoke(p, "VncAuth,None");
        org.sikuli.basics.Settings.MoveMouseDelay = 0.1f;
        for (String x : a) if (x.startsWith("-move=")) org.sikuli.basics.Settings.MoveMouseDelay = Float.parseFloat(x.substring(6));

        caption("Connecting to the QNX 8.0 target through the OpticNerve agent");
        sshFallback = a[0].equals("-") ? null : a[0];
        s = VNCScreen.start(target(a[0]), vncPort(), vncPassword(a[1]), 10, 1000);
        if (s == null || s.capture() == null) throw new IllegalStateException("no frame from " + target(a[0]) + ":" + vncPort());
        wake();
        caption("A fresh Widget Factory: closed, then opened from the Applications menu and maximized");
        ssh("rs slay -f gtk4-widget-factory >/dev/null 2>&1; sleep 1");
        click(55, 12); // Applications
        Thread.sleep(1200);
        s.mouseMove(s.newLocation(70, 202)); // Development
        Thread.sleep(1200);
        s.mouseMove(s.newLocation(200, 202));
        s.mouseMove(s.newLocation(266, 202));
        s.mouseMove(s.newLocation(266, 486)); // Widget Factory (GTK+ 4)
        Thread.sleep(500);
        click(266, 486);
        // It opens maximized if it was when it closed, otherwise somewhere else, in different sizes: found by its tab
        // strip (host/demo/ref/wf_tabs.png), at the very top when maximized; otherwise its title-bar buttons (minimize,
        // maximize, close: host/demo/ref/wf_window_buttons.png) are found and the middle one clicked.
        org.sikuli.script.Pattern tabStrip = new org.sikuli.script.Pattern(new java.io.File("host/demo/ref/wf_tabs.png").getAbsolutePath()).similar(0.8);
        org.sikuli.script.Pattern buttons = new org.sikuli.script.Pattern(new java.io.File("host/demo/ref/wf_window_buttons.png").getAbsolutePath()).similar(0.8);
        org.sikuli.script.Match strip = s.exists(tabStrip, 20);
        if (strip == null) throw new IllegalStateException("Widget Factory did not open: its tab strip is not on the screen");
        for (int attempt = 0; attempt < 3 && strip != null && strip.getCenter().y > 60; attempt++) { // not maximized
            Thread.sleep(1000); // the window drawn in full before its button is clicked
            org.sikuli.script.Match b = s.exists(buttons, 5);
            if (b == null) throw new IllegalStateException("Widget Factory's window buttons are not on the screen");
            click(b.getCenter().x, b.getCenter().y);
            away();
            Thread.sleep(1000);
            strip = s.exists(tabStrip, 5);
        }
        Thread.sleep(1500);
        org.sikuli.script.Match top = s.exists(new org.sikuli.script.Pattern(
                new java.io.File("host/demo/ref/wf_tabs.png").getAbsolutePath()).similar(0.8), 10);
        if (top == null || top.getCenter().y > 60)
            throw new IllegalStateException("Widget Factory is not maximized: its tab strip is " + (top == null ? "not found" : "at y=" + top.getCenter().y));
        long start = System.nanoTime();

        if (onlyPage != null && !onlyPage.equals("1")) {
            if (onlyPage.equals("2")) page2();
            if (onlyPage.equals("3")) page3();
            scoreboard((System.nanoTime() - start) / 1e9);
            return;
        }
        caption("Page 1: the jumps");
        page = "1";
        // As for page 2: check that page 1 is shown (its first toggle button), the tab's click can be lost.
        String first = "";
        for (int i = 0; i < 3 && !first.contains("toggle"); i++) {
            click(826, 46);
            away();
            Thread.sleep(800);
            first = ocr(new int[] { 568, 86, 120, 24 });
        }
        if (!first.contains("toggle")) throw new IllegalStateException("page 1 is not shown: OCR read \"" + first + "\"");

        // Entries
        obstacle("combo box entry: type and read back", entry(260, 98, box(260, 98, 480, 30), "jump one", null));
        obstacle("combo box entry: its list opens", opens(538, 98, new int[] { 0, 112, 700, 300 }, "list"));
        obstacle("disabled combo box entry", disabled(260, 143, box(270, 143, 540, 30)));
        obstacle("entry: its mode icon (no visible change)", unchanged(540, 187, box(280, 187, 560, 32), REAL));
        obstacle("disabled entry", disabled(260, 231, box(280, 231, 540, 30)));
        obstacle("entry: type and read back", entry(260, 275, box(260, 275, 480, 30), "bump two", null));
        obstacle("entry: its clear button (the text stays)", keepsText(538, 275, box(260, 275, 480, 30), "bump two"));
        obstacle("password entry: hidden, then shown by its eye", password());

        // Buttons in the right-hand column
        obstacle("toggle button", toggle(628, 98, box(628, 98, 96, 20)));
        obstacle("disabled toggle button", disabled(628, 143, box(628, 143, 96, 20)));
        obstacle("toggle button, pressed at the start", toggle(628, 187, box(628, 187, 96, 20)));
        obstacle("drop-down (Andrea): its list opens", opens(628, 275, new int[] { 560, 290, 300, 250 }, "list"));
        obstacle("disabled drop-down (Otto)", disabled(628, 319, box(628, 319, 120, 34)));
        obstacle("font button: its dialog opens", dialog(628, 363, "font dialog"));
        obstacle("colour button: its dialog opens", dialog(628, 407, "colour dialog"));
        obstacle("link button: the browser shows www.gtk.org", link(628, 451));
        obstacle("switch", sw(628, 492));
        obstacle("disabled switch", disabled(628, 527, box(628, 527, 40, 16)));

        // Drop-downs, spin button
        obstacle("drop-down (Left): its list opens", opens(95, 363, new int[] { 0, 382, 190, 110 }, "list"));
        obstacle("drop-down (Middle): its list opens", opens(280, 363, new int[] { 282, 382, 93, 110 }, "list"));
        obstacle("drop-down (Right): its list opens", opens(465, 363, new int[] { 370, 382, 190, 110 }, "list"));
        obstacle("spin button: + then -", spin());

        // Check and radio buttons
        obstacle("check button 1", toggle(22, 447, box(22, 447, 12, 12)));
        obstacle("check button 2", toggle(22, 479, box(22, 479, 12, 12)));
        obstacle("check button 3 (mixed): stays drawn mixed", unchanged(22, 511, box(22, 511, 24, 24), "drawn mixed, as GTK draws it until the application clears it"));
        obstacle("disabled check button", disabled(22, 543, box(22, 543, 24, 24)));
        obstacle("radio buttons: select another, then the first", radios());
        obstacle("disabled radio button", disabled(147, 575, box(147, 575, 24, 24)));

        // Scales
        obstacle("horizontal scale", slider(934, 174, 1060, 174, new int[] { 700, 160, 470, 30 }));
        obstacle("horizontal scale with marks", slider(934, 246, 1060, 246, new int[] { 700, 230, 470, 34 }));
        obstacle("vertical scale: its value follows", vscale());

        // The list and the text view
        obstacle("list: a click on a Cool check box selects its row (the box stays)", selectsRow(1578, 162, new int[] { 1630, 152, 120, 20 }));
        obstacle("text view: type, then undo", textView());

        // Notebooks
        obstacle("notebook (Sunset, NYC, Beach)", tabs(new int[] { 12, 770, 466, 260 },
                new int[][] { { 53, 753 }, { 122, 753 }, { 189, 753 } }));
        obstacle("notebook, tabs on the right", tabs(new int[] { 880, 740, 80, 140 },
                new int[][] { { 920, 761 }, { 920, 805 }, { 920, 849 } }));
        obstacle("notebook, tabs at the bottom", tabs(new int[] { 970, 1000, 260, 40 },
                new int[][] { { 1006, 1018 }, { 1080, 1018 }, { 1155, 1018 } }));
        obstacle("notebook, tabs on the left", tabs(new int[] { 1440, 740, 80, 140 },
                new int[][] { { 1477, 761 }, { 1477, 805 }, { 1477, 849 } }));

        if (onlyPage == null) {
            page2();
            page3();
        }

        scoreboard((System.nanoTime() - start) / 1e9);
    }

    static void page2() throws Exception {
        caption("Page 2: the bumps");
        page = "2";
        // The tab's click can be lost (the first click after the window was maximized): check that page 2 is shown
        // (its "Row 1"), or a later obstacle could act on page 1 and pass by chance.
        String landmark = "";
        for (int i = 0; i < 3 && !landmark.contains("Row"); i++) {
            click(958, 46);
            away();
            Thread.sleep(800);
            landmark = ocr(new int[] { 14, 520, 90, 26 });
        }
        if (!landmark.contains("Row")) throw new IllegalStateException("page 2 is not shown: OCR read \"" + landmark + "\"");

        // The spin buttons and scale buttons at the top left
        obstacle("vertical spin button: + to a multiple of 3, its message, Reset, close", multipleOf3());
        obstacle("disabled spin button", disabled(72, 99, box(72, 132, 40, 90)));
        obstacle("volume button: its slider pops up", dialog(116, 112, "slider", 300));

        // The language list and the buttons under it
        obstacle("text list: a double click selects a line, a click clears it", lineSelect(50, 310, 60, 232));
        obstacle("linked toggle buttons: picture, then flag", pair(157, 473, 122, 473, new int[] { 106, 460, 70, 26 }));
        obstacle("gear button: its menu opens", dialog(242, 473, "menu", 600));

        // The rows
        obstacle("row 1: switch", sw(230, 533));
        obstacle("row 2: scale", slider(180, 584, 225, 584, new int[] { 110, 570, 145, 28 }));
        obstacle("row 4: spin button + then -", spinAt(237, 686, 201, 686, new int[] { 145, 674, 32, 24 }));
        obstacle("row 5: its button opens a dialog", dialog(237, 737, "dialog"));
        obstacle("row 6: check button", toggle(247, 788, box(247, 788, 12, 12)));

        // The editor
        obstacle("menu File opens", dialog(298, 96, "menu", 500));
        obstacle("menu Edit opens", dialog(337, 96, "menu", 500));
        obstacle("menu View opens", dialog(380, 96, "menu", 500));
        obstacle("menu Help opens", dialog(426, 96, "menu", 150));
        obstacle("toolbar: New (no visible change)", unchanged(302, 132, new int[] { 280, 150, 1360, 400 }, REAL));
        obstacle("toolbar: Save (no visible change)", unchanged(344, 132, new int[] { 280, 150, 1360, 400 }, REAL));
        obstacle("toolbar: Search, its bar: type, erase, close", search());
        obstacle("toolbar: disabled button", disabled(433, 132, box(433, 132, 24, 24)));
        obstacle("toolbar: colour button, its dialog opens", dialog(478, 132, "colour dialog"));
        obstacle("text editor: type, then BackSpace", typeBack(300, 162, new int[] { 282, 150, 600, 26 }));

        // The panes
        obstacle("pane divider, left and right", slider(306, 830, 420, 830, new int[] { 280, 620, 680, 420 }));
        obstacle("pane divider, top and bottom", slider(1306, 642, 1306, 760, new int[] { 970, 620, 670, 420 }));

        // The buttons with dialogs (before the expander, which moves them when it closes)
        obstacle("Inform: its dialog opens", dialog(1782, 620, "dialog"));
        obstacle("Act: its dialog opens", dialog(1782, 664, "dialog"));
        obstacle("Configure: its dialog opens", dialog(1782, 708, "dialog"));
        obstacle("Select: its dialog opens", dialog(1782, 752, "dialog"));

        // The calendar
        obstacle("calendar: next month, then back", calendarStep(1778, 100, 1675, 100, new int[] { 1688, 88, 90, 24 }, "December", "November"));
        obstacle("calendar: next year, then back", calendarStep(1890, 100, 1812, 100, new int[] { 1822, 88, 60, 24 }, "2027", "2026"));
        obstacle("calendar: another day, then back", calendarDay());

        // The expander and its icons
        refocus(); // the calendar keeps the keyboard; the expander's click must not go to focus
        obstacle("expander: closes and opens", toggle(1665, 318, new int[] { 1656, 334, 262, 200 }));
        obstacle("icons: larger (+), then normal size (1)", pair(1708, 577, 1743, 577, new int[] { 1656, 334, 262, 200 }));
        obstacle("icons: larger (+), then smaller (-)", pair(1708, 577, 1674, 577, new int[] { 1656, 334, 262, 200 }));

    }

    static void page3() throws Exception {
        caption("Page 3: the thumps");
        page = "3";
        neutral = new int[] { 1300, 46 };
        String landmark = ""; // the tree's first row, "Charlemagne"
        for (int i = 0; i < 3 && !landmark.contains("Charle"); i++) {
            click(1091, 46);
            away();
            Thread.sleep(800);
            landmark = ocr(new int[] { 28, 110, 110, 20 });
        }
        if (!landmark.contains("Charle")) throw new IllegalStateException("page 3 is not shown: OCR read \"" + landmark + "\"");

        // The family tree
        // (the triangle's click selects its row too: the rows under it are checked)
        obstacle("tree: a branch closes by its triangle, and opens again", expander(75, 185, new int[] { 12, 198, 430, 180 }));
        obstacle("tree: a click selects a row", selects(109, 251, new int[] { 12, 242, 430, 18 }));
        int[] treeRows = { 12, 108, 430, 270 };
        obstacle("tree: the Name header (no sorting)", unchanged(37, 95, treeRows, REAL));
        obstacle("tree: the Birth header (no sorting)", unchanged(264, 95, treeRows, REAL));
        obstacle("tree: the Death header (no sorting)", unchanged(345, 95, treeRows, REAL));
        obstacle("tree: the wheel scrolls it, and back", scroll(230, 250, new int[] { 12, 108, 430, 270 }, new int[] { 28, 110, 110, 20 }, "Charle"));
        obstacle("Wikipedia link: the browser shows Wikipedia", link(101, 394, "wikipedia.org", "free encyclopedia")); // the page's own "Wikipedia" is not enough

        // The toolbar
        obstacle("Open: its menu opens", dialog(510, 99, "menu", 1000));
        obstacle("Record: a toggle button", toggle(605, 99, new int[] { 565, 91, 80, 16 }));
        obstacle("linked toggle buttons: list, then grid", pair(714, 99, 680, 99, new int[] { 664, 86, 70, 28 }));
        obstacle("gear button: its Settings dialog opens", dialog(756, 99, "Settings dialog", 1500));
        obstacle("Lock: the toolbar locked, and unlocked", toggle(1121, 99, new int[] { 476, 86, 690, 28 }));
        obstacle("menu button (three dots): its menu opens, a second click closes it", menuButton(1189, 99, "menu", 1000));

        // The stack and its sidebar, the entries
        obstacle("stack sidebar: Birthdate, Address, Pages, Identity", tabs(new int[] { 580, 135, 620, 160 },
                new int[][] { { 517, 150 }, { 521, 188 }, { 518, 226 }, { 512, 264 } }));
        // (lower case: typed capitals arrived as lower case, as other Shift characters did not arrive as typed)
        obstacle("entry First Name: type and read back", entry(675, 191, box(675, 191, 160, 30), "ann", null));
        obstacle("entry Last Name: type and read back", entry(675, 235, box(675, 235, 160, 30), "smith", null));
        obstacle("entry Name: type and read back", entry(1380, 99, box(1380, 99, 320, 30), "otto", null));
        obstacle("entry Age: type and read back", entry(1700, 99, box(1680, 99, 240, 30), "42", null));

        // The text view and the buttons at its left, each of which inserts its icon
        int[] firstLine = { 510, 312, 300, 24 };
        obstacle("text view: the open button inserts its icon", selects(491, 329, firstLine));
        obstacle("text view: the second button inserts its icon", selects(491, 362, firstLine));
        obstacle("text view: the full-screen button inserts its icon", selects(491, 395, firstLine));
        obstacle("text view: the star button inserts its icon", selects(491, 428, firstLine));
        obstacle("text view: disabled Cut", disabled(1189, 329, box(1189, 329, 22, 22)));
        obstacle("text view: disabled Copy", disabled(1189, 362, box(1189, 362, 22, 22)));
        obstacle("text view: disabled Paste", disabled(1189, 395, box(1189, 395, 22, 22)));
        obstacle("text view: disabled Clear", disabled(1189, 429, box(1189, 429, 22, 22)));
        obstacle("text view: type on a new line, then BackSpace", typeLine(800, 380, new int[] { 512, 334, 300, 24 }, "bump"));

        // The notebooks, judged by the line under the selected tab: after typing, the tab clicked last has a focus
        // ring, and Page 1 of the first holds a video whose controls come and go with the pointer.
        obstacle("notebook with closable tabs: Page 2, then Page 1", tabs(new int[] { 476, 486, 236, 8 },
                new int[][] { { 513, 474 }, { 609, 474 } }));
        int[] strip = { 476, 462, 200, 24 };
        obstacle("closable tab Page 2: its x closes it, it comes back", closeTab(646, 474, strip, "Page2"));
        obstacle("closable tab Page 1: its x closes it, it comes back", closeTab(548, 474, strip, "Page1"));
        obstacle("notebook (Page 1 to 5)", tabs(new int[] { 1220, 158, 650, 8 },
                new int[][] { { 1257, 146 }, { 1331, 146 }, { 1406, 146 }, { 1481, 146 }, { 1556, 146 } }));

        // The colour list at the left, and the colour editor. (The swatches first: after a colour was typed in the
        // editor and its sliders dragged, a swatch's row was selected but the editor did not take its colour.)
        obstacle("colour list: two swatches select their rows, the editor takes their colours", swatch(430, 509, "C98286", 430, 469, "C8828C"));
        obstacle("colour list: the wheel scrolls it, and back", scroll(230, 720, new int[] { 12, 420, 430, 610 }, new int[] { 14, 424, 80, 20 }, "Red"));
        // (a name, not #9141AC: "#" and the capitals need Shift, which did not arrive as typed: "#91$1ac")
        obstacle("colour editor: a colour typed in its entry", hexEntry("purple", 0x800080));
        int[] preview = { 1432, 298, 150, 28 };
        obstacle("colour editor: the hue slider", slider(1407, 600, 1407, 450, new int[] { 1430, 344, 296, 296 }));
        obstacle("colour editor: the colour plane", slider(1500, 420, 1650, 560, preview));
        obstacle("colour editor: the alpha slider", slider(1718, 664, 1560, 664, preview));
    }

    // A menu button: a click opens its menu (at least `least` sample points change), a second click on the button
    // closes it, as with the real mouse (Escape did not close it).
    static Run menuButton(int x, int y, String what, int least) {
        return () -> {
            away();
            BufferedImage before = s.capture().getImage();
            click(x, y);
            away();
            boolean[][] area = null;
            int opened = 0;
            for (long end = System.nanoTime() + 6_000_000_000L; opened < least && System.nanoTime() < end; Thread.sleep(200)) {
                Thread.sleep(300);
                area = changedPoints(before, s.capture().getImage());
                opened = count(area);
            }
            if (opened < least) throw new Fail("no " + what + " opened (" + opened + " sample points changed)");
            click(x, y);
            away();
            int left = opened;
            for (long end = System.nanoTime() + 4_000_000_000L; left > opened / 10 && System.nanoTime() < end; Thread.sleep(200)) {
                boolean[][] now = changedPoints(before, s.capture().getImage());
                left = 0;
                for (int i = 0; i < now.length; i++)
                    for (int j = 0; j < now[i].length; j++)
                        if (now[i][j] && area[i][j]) left++;
            }
            if (left > opened / 10) throw new Fail("a second click did not close the " + what + " (" + left + " of its " + opened + " sample points still changed)");
            return what + " opened (" + opened + " sample points), and closed by a second click";
        };
    }

    // A closable tab: its x closes it (OCR no longer finds its label), and Widget Factory puts it back after about
    // 5 s (David, 2026-11-18).
    static Run closeTab(int x, int y, int[] strip, String label) {
        return () -> {
            String t = tabText(strip);
            if (!t.contains(label)) throw new Fail("OCR does not find \"" + label + "\" in the tabs: \"" + t + "\"");
            click(x, y);
            away();
            long t0 = System.nanoTime();
            for (long end = t0 + 3_000_000_000L; t.contains(label) && System.nanoTime() < end; Thread.sleep(150)) t = tabText(strip);
            if (t.contains(label)) throw new Fail("its x did not close it");
            for (long end = t0 + 12_000_000_000L; !t.contains(label) && System.nanoTime() < end; Thread.sleep(300)) t = tabText(strip);
            if (!t.contains(label)) throw new Fail("it did not come back within 12 s: OCR read \"" + t + "\"");
            return String.format("closed, and back after %.1f s", (System.nanoTime() - t0) / 1e9);
        };
    }

    // The tab labels as OCR reads them, made comparable: it read "Page 1 x Page 2 x" as "Pagel x Page2 x".
    static String tabText(int[] strip) {
        return ocr(strip).replace(" ", "").replace("Pagel", "Page1");
    }

    // A tree's expander triangle. After OculiX's quick move (MoveMouseDelay 0.1 s) the click only selected the row
    // (11 of 11, also with a 300 ms rest on the triangle before the click); after its default move (0.5 s) it closed
    // and opened the branch (3 of 3).
    static Run expander(int x, int y, int[] r) {
        return () -> {
            away();
            BufferedImage before = grab(r);
            for (int i = 0; i < 2; i++) {
                float quick = org.sikuli.basics.Settings.MoveMouseDelay;
                org.sikuli.basics.Settings.MoveMouseDelay = 0.5f;
                s.mouseMove(s.newLocation(x, y));
                org.sikuli.basics.Settings.MoveMouseDelay = quick;
                Thread.sleep(300);
                click(x, y);
                away();
                if (!await(r, before, i == 1, 5)) throw new Fail(i == 0 ? "the click did not close the branch" : "a second click did not open it again");
            }
            return "closed, and opened again";
        };
    }

    // The mouse wheel: three notches one way change the region, three the other way bring back its first line (OCR
    // finds `top` in it: the pixels did not always come back exactly).
    static Run scroll(int x, int y, int[] r, int[] first, String top) {
        return () -> {
            s.mouseMove(s.newLocation(x, y));
            Thread.sleep(300);
            BufferedImage before = grab(r);
            s.wheel(org.sikuli.script.Button.WHEEL_DOWN, 3);
            away();
            if (!await(r, before, false, 3)) throw new Fail("the wheel did not scroll it");
            String gone = ocr(first);
            if (gone.contains(top)) throw new Fail("the first line is still there: OCR read \"" + gone + "\"");
            s.mouseMove(s.newLocation(x, y));
            s.wheel(org.sikuli.script.Button.WHEEL_UP, 3);
            away();
            String back = waitText(first, top, 4);
            if (!back.contains(top)) throw new Fail("the wheel back did not bring back the first line: OCR read \"" + back + "\"");
            return "scrolled 3 notches, and back to \"" + back + "\"";
        };
    }

    // A text view: a new line after the text (End, Enter), a word typed on it and read with OCR; BackSpace takes the word
    // and the line out again. (On the first line, beside the icons the buttons inserted, OCR read 777 as TTT and bump
    // as oUMp.)
    static Run typeLine(int x, int y, int[] line2, String word) {
        return () -> {
            click(x, y);
            s.type(Key.END);
            s.type("\r");
            typeSlowly("  " + word); // indented: at the very edge, OCR read the first letter wrong ("oump")
            String typed = waitText(line2, word, 3);
            if (!typed.contains(word)) throw new Fail("the typing did not show: OCR read \"" + typed + "\"");
            for (int i = 0; i <= word.length() + 2; i++) s.type(Key.BACKSPACE);
            String after = ocr(line2);
            for (long end = System.nanoTime() + 2_000_000_000L; after.contains(word) && System.nanoTime() < end; ) after = ocr(line2);
            if (after.contains(word)) throw new Fail("BackSpace did not take it out: OCR read \"" + after + "\"");
            return "typed \"" + word + "\" on a new line, and took it out with BackSpace";
        };
    }

    // The colour editor's entry: a colour typed (and Enter) is drawn in its preview.
    static Run hexEntry(String name, int want) {
        return () -> {
            selectAll(1664, 312);
            typeSlowly(name);
            s.type("\r");
            away();
            String hex = String.format("%06X", want);
            int got = -1;
            for (long end = System.nanoTime() + 3_000_000_000L; System.nanoTime() < end; Thread.sleep(150)) {
                got = s.capture(1507, 312, 1, 1).getImage().getRGB(0, 0) & 0xffffff;
                if (near(got, want)) break;
            }
            if (!near(got, want)) throw new Fail(String.format("the preview is #%06X, not #%s", got, hex));
            return String.format("typed %s, the preview is #%06X", name, got);
        };
    }

    static boolean near(int a, int b) {
        for (int sh = 0; sh <= 16; sh += 8)
            if (Math.abs(((a >> sh) & 255) - ((b >> sh) & 255)) > 6) return false;
        return true;
    }

    // Two swatches in the colour list: each one's row is selected and the colour editor's entry shows its colour. (The
    // colour chooser keeps its colour in GTK's settings from one run to the next: so the colours are checked, not a
    // change.)
    static Run swatch(int x1, int y1, String hex1, int x2, int y2, String hex2) {
        return () -> {
            int[] field = { 1602, 300, 124, 24 };
            click(x1, y1);
            away();
            String got = waitText(field, hex1, 5);
            if (!got.contains(hex1)) throw new Fail("after the first swatch the editor's entry reads \"" + got + "\", not #" + hex1);
            click(x2, y2);
            away();
            got = waitText(field, hex2, 5);
            if (!got.contains(hex2)) throw new Fail("after the second swatch the editor's entry reads \"" + got + "\", not #" + hex2);
            return "the editor showed #" + hex1 + ", then #" + hex2;
        };
    }

    // The Developer Desktop blanks the screen after some idle minutes, and then shows "Unlock your desktop" with a
    // green circle to click (no password). Moves the pointer to wake it, and clicks the circle if it is there.
    static void wake() throws Exception {
        s.mouseMove(s.newLocation(900, 500));
        s.mouseMove(s.newLocation(960, 540));
        Thread.sleep(1500);
        String lock = ocr(new int[] { 660, 380, 600, 320 });
        if (lock.contains("Unlock")) {
            say("     ", "the desktop was locked: clicking its unlock circle");
            click(958, 550);
            Thread.sleep(1500);
        }
    }

    // Runs commands on the target over SSH (host/tools/pi_ssh.sh, which reads ON_LOGIN_PASSWORD); returns the output.
    static String ssh(String commands) throws Exception {
        Process p = pi_ssh(commands).start();
        String out = new String(p.getInputStream().readAllBytes()).trim();
        p.waitFor();
        return out;
    }

    // The link button opens www.gtk.org in a new web browser, which takes about 10 s to come up (David, 2026-11-17).
    // One click, then up to 30 s for the page: OCR finds "gtk.org" or the site's text on the screen. Then the browser
    // is closed over SSH (cleanup; its window opens in different places and Ctrl from OculiX does not reach it).
    static Run link(int x, int y) {
        return link(x, y, "gtk.org", "create interfaces");
    }

    // The same for a link to another site: the page is there when OCR finds one of `words` on the screen.
    static Run link(int x, int y, String... words) {
        return () -> {
            ssh("rs slay -f epiphany >/dev/null 2>&1"); // no browser left from before
            click(x, y);
            away();
            int[] all = { 0, 20, 1920, 1040 };
            long t0 = System.nanoTime();
            String seen = "";
            boolean found = false;
            while (!found && System.nanoTime() - t0 < 30_000_000_000L) {
                Thread.sleep(1000);
                seen = ocr(all);
                String low = seen.toLowerCase();
                for (String w : words) found |= low.contains(w);
            }
            double secs = (System.nanoTime() - t0) / 1e9;
            String browsers = ssh("rs pidin ar 2>/dev/null | grep -c '[e]piphany'");
            ssh("rs slay -f epiphany >/dev/null 2>&1");
            Thread.sleep(1000);
            refocus();
            if (!found) throw new Fail("no " + words[0] + " page within 30 s (browser processes: " + browsers + ")");
            return String.format("%s shown after %.1f s; the browser closed (over SSH)", words[0], secs);
        };
    }

    // A click on an entry's button that leaves its text as it is, as with the real mouse.
    static Run keepsText(int x, int y, int[] r, String text) {
        nextKind = "none";
        return () -> {
            click(x, y);
            Thread.sleep(800);
            String t = ocr(r);
            if (!t.contains(text)) throw new Fail("the text changed: OCR read \"" + t + "\"");
            return "\"" + text + "\" stays, as with the real mouse";
        };
    }

    // A click in a list row selects it (its name cell changes to the selected colours); the check box in it does not
    // respond to clicks, as with the real mouse.
    static Run selectsRow(int x, int y, int[] name) {
        nextKind = "none";
        return () -> {
            away();
            BufferedImage before = grab(name);
            click(x, y);
            away();
            if (!await(name, before, false, 2) && !isSelected(name))
                throw new Fail("the row was not selected");
            return "row selected; the check box does not respond to clicks, as with the real mouse";
        };
    }

    // The row's name cell has the selection's blue behind it.
    static boolean isSelected(int[] r) {
        int c = s.capture(r[0] + r[2] - 4, r[1] + r[3] / 2, 1, 1).getImage().getRGB(0, 0);
        return (c & 255) > 180 && ((c >> 16) & 255) < 120;
    }

    // A click that must not change what is drawn (with the reason).
    static Run unchanged(int x, int y, int[] r, String why) {
        nextKind = "none";
        return () -> {
            away();
            BufferedImage before = grab(r);
            click(x, y);
            away();
            if (await(r, before, false, 1)) throw new Fail("it changed");
            return why;
        };
    }

    // Two clicks, each changing the region: for a check box in a list, whose row stays selected after a click, so
    // the region does not come back exactly.
    static Run flipTwice(int x, int y, int[] r) {
        return () -> {
            away();
            BufferedImage before = grab(r);
            click(x, y);
            away();
            if (!await(r, before, false, 2)) throw new Fail("the click changed nothing");
            BufferedImage once = grab(r);
            click(x, y);
            away();
            if (!await(r, once, false, 2)) throw new Fail("a second click changed nothing");
            return "flipped, and flipped back";
        };
    }

    // Selects all the text of an entry with a triple click (Ctrl+A from OculiX arrived as a plain "a" here,
    // 2026-11-17).
    static void selectAll(int x, int y) throws Exception {
        click(x, y);
        press(x, y);
        press(x, y);
        lastClick = System.currentTimeMillis();
    }

    // A spin button's + then -: OCR reads the value up by one, then back.
    static Run spinAt(int px, int py, int mx, int my, int[] r) {
        return () -> {
            String v0 = ocr(r).replaceAll("[^0-9]", "");
            if (v0.isEmpty()) throw new Fail("OCR read no value");
            String up = String.valueOf(Integer.parseInt(v0) + 1);
            click(px, py);
            String v1 = waitText(r, up, 2).replaceAll("[^0-9]", "");
            if (!v1.equals(up)) throw new Fail("after + OCR read \"" + v1 + "\" (was \"" + v0 + "\")");
            click(mx, my);
            String v2 = waitText(r, v0, 2).replaceAll("[^0-9]", "");
            if (!v2.equals(v0)) throw new Fail("after - OCR read \"" + v2 + "\"");
            return v0 + " -> " + v1 + " -> " + v2;
        };
    }

    // A click changes the region (a list row becomes selected).
    static Run selects(int x, int y, int[] r) {
        return () -> {
            away();
            BufferedImage before = grab(r);
            click(x, y);
            away();
            if (!await(r, before, false, 2)) throw new Fail("the click changed nothing");
            return "selected";
        };
    }

    // Two buttons: the first changes the region, the second puts it back exactly.
    static Run pair(int x1, int y1, int x2, int y2, int[] r) {
        return () -> {
            away();
            BufferedImage before = grab(r);
            click(x1, y1);
            away();
            if (!await(r, before, false, 3)) throw new Fail("the first click changed nothing");
            click(x2, y2);
            away();
            if (!await(r, before, true, 3)) throw new Fail("the second click did not put it back");
            return "changed, and back";
        };
    }

    // Types at the start of a line of a text area and takes it out again with BackSpace, read with OCR.
    static Run typeBack(int x, int y, int[] r) {
        return typeBack(x, y, r, "777");
    }

    static Run typeBack(int x, int y, int[] r, String word) {
        return () -> {
            String before = ocr(r);
            click(x, y);
            s.type(Key.HOME);
            typeSlowly(word + " ");
            String typed = waitText(r, word, 3);
            if (count(typed, word) <= count(before, word)) throw new Fail("the typing did not show: OCR read \"" + typed + "\"");
            for (int i = 0; i <= word.length(); i++) s.type(Key.BACKSPACE);
            String after = ocr(r);
            for (long end = System.nanoTime() + 2_000_000_000L; count(after, word) > count(before, word) && System.nanoTime() < end; ) after = ocr(r);
            if (count(after, word) > count(before, word)) throw new Fail("BackSpace did not take it out: OCR read \"" + after + "\"");
            return "typed \"" + word + " \", and took it out with BackSpace";
        };
    }

    // The vertical spin button: + until its value is a multiple of 3; the page then shows "N is a multiple of 3" with
    // Reset and a close button (David, 2026-11-17). Reset sets the value back to 50; the close button closes the message.
    static Run multipleOf3() {
        return () -> {
            int[] value = { 10, 120, 36, 24 }, message = { 790, 80, 340, 80 };
            String v = ocr(value).replaceAll("[^0-9]", "");
            if (v.isEmpty()) throw new Fail("OCR read no value");
            int n = Integer.parseInt(v), clicks = 0;
            while (n % 3 != 0 && clicks < 3) {
                click(28, 99);
                n++;
                clicks++;
                String want = String.valueOf(n);
                String got = waitText(value, want, 2).replaceAll("[^0-9]", "");
                if (!got.equals(want)) throw new Fail("after + OCR read \"" + got + "\", not " + want);
            }
            String msg = waitText(message, "multiple", 3);
            if (!msg.contains("multiple")) throw new Fail("at " + n + " no message: OCR read \"" + msg + "\"");
            click(1004, 120); // Reset
            String back = waitText(value, "50", 2).replaceAll("[^0-9]", "");
            if (!back.equals("50")) throw new Fail("Reset left \"" + back + "\"");
            String still = ocr(message);
            if (still.contains("multiple")) {
                click(1077, 120); // close
                for (long end = System.nanoTime() + 2_000_000_000L; still.contains("multiple") && System.nanoTime() < end; ) still = ocr(message);
                if (still.contains("multiple")) throw new Fail("the close button did not close the message");
            }
            away();
            return v + " -> " + n + ": \"" + msg + "\"; Reset -> 50; closed";
        };
    }

    // A text area of lines: a double click selects a line (the selection's blue behind it), a click elsewhere clears it.
    // The clearing click took effect late: the text cursor on the clicked line and the cleared selection showed up
    // together after 0.6 to 3.6 s (2026-11-18, 22 repeats), where other clicks show within a few hundred ms; a pause
    // before the click did not change it. So up to 10 s are allowed, and both times are printed.
    static Run lineSelect(int x, int y, int cx, int cy) {
        return () -> {
            click(x, y);
            s.doubleClick(s.newLocation(x, y));
            lastClick = System.currentTimeMillis();
            away();
            boolean sel = false;
            for (long end = System.nanoTime() + 2_000_000_000L; !sel && System.nanoTime() < end; Thread.sleep(150))
                sel = blueIn(new int[] { 14, y - 8, 240, 16 });
            if (!sel) throw new Fail("the double click selected nothing");
            int[] target = { 14, cy - 9, 240, 18 };
            BufferedImage tb = grab(target);
            click(cx, cy);
            away();
            boolean gone = false;
            long c0 = System.nanoTime();
            double caret = -1;
            for (long end = c0 + 10_000_000_000L; !gone && System.nanoTime() < end; Thread.sleep(50)) {
                gone = !blueIn(new int[] { 14, y - 8, 240, 16 });
                if (caret < 0 && !same(tb, grab(target))) caret = (System.nanoTime() - c0) / 1e9;
            }
            say("            ", String.format("(caret on the clicked line after %.1f s, cleared after %.1f s)", caret, (System.nanoTime() - c0) / 1e9));
            if (!gone) {
                int[] line = { 14, y - 8, 240, 16 };
                javax.imageio.ImageIO.write(grab(line), "png", new java.io.File("demo-out", "textlist_line.png"));
                throw new Fail("a click did not clear the selection (" + blueCount(line) + " blue pixels left; the line is in demo-out\\textlist_line.png)");
            }
            return "line selected, and cleared";
        };
    }

    // Whether a region has the selection's blue in it.
    static int blueCount(int[] r) {
        BufferedImage img = grab(r);
        int n = 0;
        for (int y = 0; y < img.getHeight(); y++)
            for (int x = 0; x < img.getWidth(); x++) {
                int c = img.getRGB(x, y);
                if ((c & 255) > 180 && (c & 255) > ((c >> 16) & 255) + 25) n++;
            }
        return n;
    }

    static boolean blueIn(int[] r) {
        BufferedImage img = grab(r);
        for (int y = 0; y < img.getHeight(); y++)
            for (int x = 0; x < img.getWidth(); x++) {
                int c = img.getRGB(x, y);
                int blu = c & 255, red = (c >> 16) & 255;
                if (blu > 180 && blu > red + 25) return true; // the selection's blue, strong or pale (window inactive)
            }
        return false;
    }

    // The editor's Search button shows a search bar; typed text appears in its field; the field's erase button
    // clears it; Escape in the field hides the bar. (Enter does not search, as with the real mouse.)
    static Run search() {
        return () -> {
            int[] bar = { 800, 158, 340, 36 };
            away();
            BufferedImage before = grab(bar);
            click(391, 132);
            away();
            boolean shown = false; // its field gets a blinking cursor, so the region never settles: any change will do
            for (long end = System.nanoTime() + 3_000_000_000L; !shown && System.nanoTime() < end; Thread.sleep(150))
                shown = !same(before, grab(bar));
            if (!shown) throw new Fail("no search bar");
            Thread.sleep(500);
            click(960, 176);
            typeSlowly("777");
            int[] field = { 872, 164, 140, 26 }; // the field's text (OCR read nothing in the whole bar)
            String typed = waitText(field, "777", 3);
            if (!typed.contains("777")) throw new Fail("OCR read \"" + typed + "\" in the search field");
            click(1052, 176); // the field's erase button
            String after = ocr(field);
            for (long end = System.nanoTime() + 2_000_000_000L; after.contains("777") && System.nanoTime() < end; ) after = ocr(field);
            if (after.contains("777")) throw new Fail("the erase button left \"" + after + "\"");
            click(960, 176); // Escape in the field closes the bar (so does its close button; Search again does nothing, as with the real mouse)
            s.type(Key.ESC);
            away();
            if (!await(bar, before, true, 3)) throw new Fail("Escape did not hide the bar");
            return "bar shown, \"777\" typed and erased, bar hidden with Escape";
        };
    }

    // A switch: a click moves its knob to the other end, a second click back. Judged by where the knob is (its white
    // disc left or right), not by every pixel: the switch's exact pixels did not always come back (2026-11-17).
    static Run sw(int x, int y) {
        return () -> {
            boolean on0 = knobRight(x, y);
            click(x, y);
            away();
            boolean moved = false;
            for (long end = System.nanoTime() + 3_000_000_000L; !moved && System.nanoTime() < end; Thread.sleep(150))
                moved = knobRight(x, y) != on0;
            if (!moved) throw new Fail("the click did not move the knob");
            click(x, y);
            away();
            boolean back = false;
            for (long end = System.nanoTime() + 3_000_000_000L; !back && System.nanoTime() < end; Thread.sleep(150))
                back = knobRight(x, y) == on0;
            if (!back) throw new Fail("a second click did not move the knob back");
            return (on0 ? "on" : "off") + " -> " + (on0 ? "off" : "on") + " -> " + (on0 ? "on" : "off");
        };
    }

    // Whether a switch's knob (a near-white disc) is at its right end: the right end is brighter than the left.
    static boolean knobRight(int x, int y) {
        BufferedImage img = s.capture(x - 22, y, 45, 1).getImage();
        return sum(img.getRGB(44, 0)) > sum(img.getRGB(0, 0)) + 30 || (sum(img.getRGB(44, 0)) > 720 && sum(img.getRGB(0, 0)) < 600);
    }

    static int sum(int c) {
        return ((c >> 16) & 255) + ((c >> 8) & 255) + (c & 255);
    }

    // A calendar day: the calendar opens on today, drawn selected (blue). The day a row above (or below) is clicked
    // and must be drawn selected, then today again.
    static Run calendarDay() {
        return () -> {
            int[] grid = { 1655, 108, 260, 130 }, today = null;
            for (long end = System.nanoTime() + 3_000_000_000L; today == null && System.nanoTime() < end; Thread.sleep(150))
                today = blueCentre(grid);
            if (today == null) throw new Fail("no day is drawn selected");
            int x0 = today[0], y0 = today[1], x = x0, y = y0 > 150 ? y0 - 26 : y0 + 26;
            click(x, y);
            away();
            boolean moved = false;
            for (long end = System.nanoTime() + 3_000_000_000L; !moved && System.nanoTime() < end; Thread.sleep(150))
                moved = blue(x, y) && !blue(x0, y0);
            if (!moved) throw new Fail("the clicked day was not selected");
            click(x0, y0);
            away();
            boolean back = false;
            for (long end = System.nanoTime() + 3_000_000_000L; !back && System.nanoTime() < end; Thread.sleep(150))
                back = blue(x0, y0) && !blue(x, y);
            if (!back) throw new Fail("today was not selected again");
            return "another day selected, and today again";
        };
    }

    // The middle of the strong blue (a selected cell) in a region, or null.
    static int[] blueCentre(int[] r) {
        BufferedImage img = grab(r);
        int minX = 9999, maxX = -1, minY = 9999, maxY = -1;
        for (int y = 0; y < img.getHeight(); y++)
            for (int x = 0; x < img.getWidth(); x++) {
                int c = img.getRGB(x, y);
                if ((c & 255) > 180 && ((c >> 16) & 255) < 120) {
                    minX = Math.min(minX, x); maxX = Math.max(maxX, x);
                    minY = Math.min(minY, y); maxY = Math.max(maxY, y);
                }
            }
        return maxX < 0 ? null : new int[] { r[0] + (minX + maxX) / 2, r[1] + (minY + maxY) / 2 };
    }

    // The selection's blue at a point (a little left of a day's number, inside its cell).
    static boolean blue(int x, int y) {
        int c = s.capture(x - 14, y, 1, 1).getImage().getRGB(0, 0);
        return (c & 255) > 180 && ((c >> 16) & 255) < 120;
    }

    // A calendar arrow: OCR reads the new month or year, and the other arrow brings back the first.
    static Run calendarStep(int nx, int ny, int px, int py, int[] r, String next, String back) {
        return () -> {
            String v0 = ocr(r);
            if (!v0.contains(back)) throw new Fail("OCR read \"" + v0 + "\", not \"" + back + "\"");
            click(nx, ny);
            String v1 = waitText(r, next, 4);
            if (!v1.contains(next)) throw new Fail("after the arrow OCR read \"" + v1 + "\"");
            click(px, py);
            String v2 = waitText(r, back, 4);
            if (!v2.contains(back)) throw new Fail("after the other arrow OCR read \"" + v2 + "\"");
            return v0 + " -> " + v1 + " -> " + v2;
        };
    }

    // A click changes the region (an action with more than two states, such as a mode icon).
    static Run toggleOnce(int x, int y, int[] r) {
        return () -> {
            away();
            BufferedImage before = grab(r);
            click(x, y);
            away();
            if (!await(r, before, false, 2)) throw new Fail("the click changed nothing");
            return "changed";
        };
    }

    static Run password() {
        return () -> {
            int[] r = box(260, 319, 480, 30);
            selectAll(260, 319);
            typeSlowly("thump3");
            Thread.sleep(300);
            String hidden = ocr(r);
            if (hidden.contains("thump3")) throw new Fail("the password is shown as typed: \"" + hidden + "\"");
            click(540, 319);
            String shown = waitText(r, "thump3", 3);
            if (!shown.contains("thump3")) throw new Fail("the eye did not show it: OCR read \"" + shown + "\"");
            click(540, 319);
            String again = ocr(r);
            for (long end = System.nanoTime() + 2_000_000_000L; again.contains("thump3") && System.nanoTime() < end; )
                again = ocr(r);
            if (again.contains("thump3")) throw new Fail("the eye did not hide it again");
            return "hidden, shown, hidden";
        };
    }

    static Run spin() {
        return () -> {
            int[] r = { 110, 395, 40, 26 };
            String v0 = ocr(r);
            click(202, 407);
            String v1 = waitText(r, "51", 2);
            if (!v1.contains("51")) throw new Fail("after + OCR read \"" + v1 + "\" (was \"" + v0 + "\")");
            click(167, 407);
            String v2 = waitText(r, "50", 2);
            if (!v2.contains("50")) throw new Fail("after - OCR read \"" + v2 + "\"");
            return v0 + " -> " + v1 + " -> " + v2;
        };
    }

    static Run radios() {
        return () -> {
            int[] r1 = box(147, 447, 12, 12), r2 = box(147, 479, 12, 12);
            away();
            BufferedImage one = grab(r1), two = grab(r2);
            click(147, 479);
            away();
            if (!await(r2, two, false, 2)) throw new Fail("the second radio button did not become selected");
            if (!await(r1, one, false, 2)) throw new Fail("the first radio button stayed selected");
            click(147, 447);
            away();
            if (!await(r1, one, true, 2)) throw new Fail("the first radio button did not come back");
            return "second selected, first cleared; first again";
        };
    }

    static Run vscale() {
        return () -> {
            int[] value = { 990, 285, 60, 26 };
            String v0 = ocr(value);
            s.dragDrop(s.newLocation(1018, 514), s.newLocation(1018, 400));
            away();
            String v1 = v0;
            for (long end = System.nanoTime() + 2_000_000_000L; v1.equals(v0) && System.nanoTime() < end; ) v1 = ocr(value);
            if (v1.equals(v0)) throw new Fail("its value stayed \"" + v0 + "\"");
            s.dragDrop(s.newLocation(1018, 400), s.newLocation(1018, 514));
            away();
            String v2 = v1;
            for (long end = System.nanoTime() + 2_000_000_000L; v2.equals(v1) && System.nanoTime() < end; ) v2 = ocr(value);
            return "value " + v0 + " -> " + v1 + " -> " + v2;
        };
    }

    static Run textView() {
        return () -> {
            // Checked with OCR: the text view's blinking cursor keeps its pixels changing.
            int[] r = { 1560, 420, 350, 40 };
            String before = ocr(r);
            click(1580, 432);
            s.type(Key.HOME);
            typeSlowly("777 ");
            String typed = waitText(r, "777", 3);
            if (count(typed, "777") <= count(before, "777")) throw new Fail("the typing did not show: OCR read \"" + typed + "\"");
            for (int i = 0; i < 3; i++) s.type(Key.BACKSPACE);
            String after = ocr(r);
            for (long end = System.nanoTime() + 2_000_000_000L; count(after, "777") > count(before, "777") && System.nanoTime() < end; ) after = ocr(r);
            if (count(after, "777") > count(before, "777")) throw new Fail("BackSpace did not take it out: OCR read \"" + after + "\"");
            return "typed \"777 \" at the start, and took it out with BackSpace";
        };
    }

    static int count(String text, String part) {
        int n = 0;
        for (int i = text.indexOf(part); i >= 0; i = text.indexOf(part, i + 1)) n++;
        return n;
    }

    static void scoreboard(double total) {
        caption("Scoreboard");
        for (String pg : new java.util.LinkedHashSet<>(board.stream().map(sc -> sc.page).toList())) {
            int operated = 0, refused = 0, none = 0, fail = 0, skip = 0;
            double secs = 0;
            for (Score sc : board) {
                if (!sc.page.equals(pg)) continue;
                secs += sc.secs;
                if (sc.result.equals("FAIL")) fail++;
                else if (sc.result.equals("SKIP")) skip++;
                else if (sc.kind.equals("refused")) refused++;
                else if (sc.kind.equals("none")) none++;
                else operated++;
            }
            say("", String.format("page %s: %d obstacles in %.1f s", pg, operated + refused + none + fail + skip, secs));
            say("", String.format("  %3d operated      (the control did what it should)", operated));
            say("", String.format("  %3d refused       (disabled, as drawn)", refused));
            say("", String.format("  %3d no response   (as the real mouse gets, or as GTK draws it)", none));
            say("", String.format("  %3d failed", fail));
            if (skip > 0) say("", String.format("  %3d skipped", skip));
        }
        say("", String.format("the whole course: %d obstacles, %.1f s", board.size(), total));
    }
}
