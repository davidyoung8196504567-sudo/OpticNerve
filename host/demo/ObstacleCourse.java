// The Widget Factory Obstacle Course: operates the controls of GTK 4's Widget Factory (gtk4-widget-factory) on the
// QNX 8.0 quick-start image's Developer Desktop, driven by OculiX through the OpticNerve agent, and times each one.
// Each obstacle acts on a control and checks the screen: its region must change (and settle); where the action can
// be undone, the test undoes it and the region must be back to exactly what it was, so a run leaves the page as it
// found it. Values are read with OCR. A disabled control must not change when clicked. Prints PASS or FAIL per
// obstacle with its time, then a scoreboard; the exit status is the number of failures.
//
//   java -cp "tigervnc-java-oculix-2.0.4.jar;oculixapi-4.0.0-windows.jar" host/demo/ObstacleCourse.java HOST VNCPASSWORD [-cols=N]
//
// Needs the agent running on HOST (port 5901) and Widget Factory (GTK+ 4) open and maximized on the Developer Desktop
// (Applications, Development, Widget Factory (GTK+ 4)). Page 1 so far.
import java.awt.image.BufferedImage;
import java.util.ArrayList;
import java.util.List;

import org.sikuli.script.Key;
import org.sikuli.script.KeyModifier;
import org.sikuli.vnc.VNCScreen;

public class ObstacleCourse {
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

    static void caption(String text) {
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
        String page, name, result, detail;
        double secs;
    }

    static List<Score> board = new ArrayList<>();
    static String page = "";

    static void obstacle(String name, Run r) {
        Score sc = new Score();
        sc.page = page;
        sc.name = name;
        long t0 = System.nanoTime();
        try {
            sc.detail = r.run();
            sc.result = "PASS";
        } catch (Fail f) {
            sc.result = "FAIL";
            sc.detail = f.getMessage();
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

    // OculiX's click (its mouseDown and mouseUp, with a pause between, produced no clicks here, 2026-11-17).
    static void press(int x, int y) throws Exception {
        s.click(s.newLocation(x, y));
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
            if (!await(r, before, false, 3)) throw new Fail("the click changed nothing");
            click(x, y);
            away();
            if (!await(r, before, true, 3)) throw new Fail("a second click did not put it back");
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
            away();
            if (!await(r, before, true, 3)) throw new Fail("the " + what + " did not close with Escape");
            return what + " opened, and closed with Escape";
        };
    }

    // A click opens a dialog, found by its title anywhere on the screen (a dialog does not always open in the same
    // place); Escape closes it.
    static Run dialog(int x, int y, String title) {
        return () -> {
            int[] all = { 0, 20, 1920, 1040 };
            click(x, y);
            String t = waitText(all, title, 8);
            if (!t.contains(title)) throw new Fail("no dialog \"" + title + "\" on the screen");
            s.type(Key.ESC);
            away();
            String after = ocr(all);
            for (long end = System.nanoTime() + 4_000_000_000L; after.contains(title) && System.nanoTime() < end; ) after = ocr(all);
            if (after.contains(title)) throw new Fail("the dialog \"" + title + "\" did not close with Escape");
            return "\"" + title + "\" opened, and closed with Escape";
        };
    }

    // A disabled control: a click changes nothing.
    static Run disabled(int x, int y, int[] r) {
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
        Object p = Class.forName("com.tigervnc.rfb.SecurityClient").getField("secTypes").get(null);
        p.getClass().getMethod("setParam", String.class).invoke(p, "VncAuth,None");
        org.sikuli.basics.Settings.MoveMouseDelay = 0.1f;

        caption("Connecting to the QNX 8.0 target through the OpticNerve agent");
        s = VNCScreen.start(a[0], 5901, a[1], 10, 1000);
        if (s == null || s.capture() == null) throw new IllegalStateException("no frame from " + a[0] + ":5901");
        wake();
        String tabs = ocr(new int[] { 760, 30, 400, 34 });
        if (!tabs.contains("Page 1")) throw new IllegalStateException("Widget Factory is not on the screen: OCR read \"" + tabs + "\"");
        long start = System.nanoTime();

        caption("Page 1: the jumps");
        page = "1";
        click(826, 46);
        away();
        Thread.sleep(500);

        // Entries
        obstacle("combo box entry: type and read back", entry(260, 98, box(260, 98, 480, 30), "jump one", null));
        obstacle("combo box entry: its list opens", opens(538, 98, new int[] { 0, 112, 700, 300 }, "list"));
        obstacle("disabled combo box entry", disabled(260, 143, box(270, 143, 540, 30)));
        obstacle("entry: its icon changes the mode", toggleOnce(540, 187, box(280, 187, 560, 32)));
        obstacle("disabled entry", disabled(260, 231, box(280, 231, 540, 30)));
        obstacle("entry: type, read back, clear with its button", entry(260, 275, box(260, 275, 480, 30), "bump two", new int[] { 538, 275 }));
        obstacle("password entry: hidden, then shown by its eye", password());

        // Buttons in the right-hand column
        obstacle("toggle button", toggle(628, 98, box(628, 98, 96, 20)));
        obstacle("disabled toggle button", disabled(628, 143, box(628, 143, 120, 34)));
        obstacle("toggle button, pressed at the start", toggle(628, 187, box(628, 187, 96, 20)));
        obstacle("drop-down (Andrea): its list opens", opens(628, 275, new int[] { 560, 290, 300, 250 }, "list"));
        obstacle("disabled drop-down (Otto)", disabled(628, 319, box(628, 319, 120, 34)));
        obstacle("font button: its dialog opens", dialog(628, 363, "Pick a Font"));
        obstacle("colour button: its dialog opens", dialog(628, 407, "Pick a Color"));
        skip("link button", "it opens a web page");
        obstacle("switch", toggle(628, 492, box(628, 492, 40, 16)));
        obstacle("second switch", toggle(628, 527, box(628, 527, 40, 16)));

        // Drop-downs, spin button
        obstacle("drop-down (Left): its list opens", opens(95, 363, new int[] { 0, 382, 190, 110 }, "list"));
        obstacle("drop-down (Middle): its list opens", opens(280, 363, new int[] { 185, 382, 190, 110 }, "list"));
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
        obstacle("list: Cool check box (Andrea)", flipTwice(1578, 118, box(1578, 118, 18, 18)));
        obstacle("list: Cool check box (Otto)", flipTwice(1578, 140, box(1578, 140, 18, 18)));
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

        scoreboard((System.nanoTime() - start) / 1e9);
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

    // A click that must not change what is drawn (with the reason).
    static Run unchanged(int x, int y, int[] r, String why) {
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
        int pass = 0, fail = 0, skip = 0;
        double timed = 0;
        for (Score sc : board) {
            if (sc.result.equals("PASS")) pass++;
            else if (sc.result.equals("FAIL")) fail++;
            else skip++;
            timed += sc.secs;
        }
        say("", String.format("page 1: %d obstacles, %d cleared, %d failed, %d skipped; %.1f s, %.1f s per obstacle",
                board.size(), pass, fail, skip, total, timed / Math.max(1, pass + fail)));
    }
}
