// GTK Demo Page Sanity Test: crawls the sidebar of the QNX 8.0 quick-start image's GTK Demo (gtk4-demo), driven by
// OculiX through the OpticNerve agent, with the mouse only. Opens the GTK Demo from the launcher (its tile found by
// image), then clicks each item on the screen in turn: a folder's label is clicked and the Run button must be
// disabled, then its triangle twice (the folder closes, then opens again; the triangle must turn each time); any
// other item's label is clicked once and the Run button must become enabled. When the items on the screen are done, the mouse
// wheel scrolls about a page and the test finds the last item it did, by its pixels, and carries on below it. Prints
// the tree it found, with PASS or FAIL per item; the exit status is the number of failures. It does not press Run.
// At the end the tree is compared with host/demo/ref/gtk_demo_tree.txt. A few items have a step time (STEP_MS):
// the test stays on them that long before the next one.
//
//   java -cp "tigervnc-java-oculix-2.0.4.jar;oculixapi-4.0.0-windows.jar" host/demo/GtkDemoTest.java HOST|- VNCPASSWORD|- [-record] [-step=NAME:MS] [-nostep] [-cols=N]
// A - for HOST or PASSWORD takes ON_HOST or ON_VNC_PASSWORD from the environment or the settings file
// (host/tools/opticnerve.env.example); SSH goes to ON_SSH_HOST:ON_SSH_PORT (default HOST, port 22).
//
// Needs the agent running on HOST (port ON_VNC_PORT) and ON_LOGIN_PASSWORD set (SSH closes a GTK Demo left running before
// the test, and the test's own at the end). The tree it found is also written to demo-out/gtk_demo_tree.txt; -record saves it as the reference.
import java.awt.image.BufferedImage;
import java.io.File;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.List;

import org.sikuli.script.Button;
import org.sikuli.script.Match;
import org.sikuli.script.Pattern;
import org.sikuli.vnc.VNCScreen;

public class GtkDemoTest {
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

    // A hand on the mouse and keyboard: the one way this test sends input (the model of host/tools/hand.py; the same
    // block in each Java test, kept identical by tests/regression/test_hand.py). The pointer glides from where it is
    // along a slightly curved path, easing in and out, at about 60 pointer events a second (0.5 to 0.8 s by distance,
    // about 1.5 px of tremor), rests 80 to 200 ms, then clicks with the button held 50 to 90 ms: OculiX's click, with
    // MoveMouseDelay 0 so that OculiX adds no move of its own, and ClickDelay set each time (OculiX resets it after
    // each click; with 0 the press and release go out together). A double or triple click holds each press 50 to
    // 90 ms, 80 to 150 ms apart. A drag holds the button along such a path. Each key is held 40 to 80 ms and comes 60
    // to 180 ms after the last, one character at a time (typed at full speed, a Developer Desktop application got some
    // characters out of order and lost others, 2026-11-17); wheel notches come 60 to 150 ms apart. The agent does not
    // report where the pointer is, so the first move of a run starts at the bottom centre of the screen. With glides false
    // (a test's choice), the pointer jumps to the target in one move and clicks without the rest; keys keep their pace.
    static final java.util.Random hand = new java.util.Random();
    static int handX = -1, handY = -1;
    static boolean glides = true;
    static { org.sikuli.basics.Settings.MoveMouseDelay = 0; }

    static void rest(double lo, double hi) throws InterruptedException {
        Thread.sleep((long) (1000 * (lo + hand.nextDouble() * (hi - lo))));
    }

    static void glide(int x, int y) throws Exception {
        org.sikuli.basics.Settings.MoveMouseDelay = 0;
        int w = s.getW(), h = s.getH();
        x = Math.max(0, Math.min(w - 1, x));
        y = Math.max(0, Math.min(h - 1, y));
        if (handX < 0) { handX = w / 2; handY = h - 1; }
        if (!glides) {
            s.getRobot().mouseMove(x, y);
            handX = x;
            handY = y;
            return;
        }
        double dist = Math.hypot(x - handX, y - handY), dur = 0, cx = x, cy = y;
        int steps = 1;
        if (dist >= 1) {
            dur = 0.5 + 0.3 * Math.min(1, dist / 1500);
            double bend = (hand.nextDouble() * 0.3 - 0.15) * dist;
            cx = (handX + x) / 2.0 - (y - handY) / dist * bend;
            cy = (handY + y) / 2.0 + (x - handX) / dist * bend;
            steps = Math.max(2, (int) (dur * 60));
        }
        for (int i = 1; i <= steps; i++) {
            double t = (double) i / steps, e = t * t * (3 - 2 * t);
            double qx = (1 - e) * (1 - e) * handX + 2 * (1 - e) * e * cx + e * e * x;
            double qy = (1 - e) * (1 - e) * handY + 2 * (1 - e) * e * cy + e * e * y;
            if (i < steps) { qx += hand.nextDouble() * 3 - 1.5; qy += hand.nextDouble() * 3 - 1.5; }
            s.getRobot().mouseMove((int) Math.max(0, Math.min(w - 1, Math.round(qx))),
                                   (int) Math.max(0, Math.min(h - 1, Math.round(qy))));
            if (dur > 0) Thread.sleep((long) (dur * 1000 / steps));
        }
        handX = x;
        handY = y;
    }

    static void handClick(int x, int y) throws Exception {
        glide(x, y);
        if (glides) rest(0.08, 0.2);
        org.sikuli.basics.Settings.ClickDelay = 0.05 + hand.nextDouble() * 0.04;
        s.click(s.newLocation(handX, handY));
    }

    // A double click (n = 2) or a triple click (n = 3), each press held 50 to 90 ms, 80 to 150 ms apart.
    static void handClicks(int x, int y, int n) throws Exception {
        glide(x, y);
        if (glides) rest(0.08, 0.2);
        for (int i = 0; i < n; i++) {
            if (i > 0) rest(0.08, 0.15);
            s.getRobot().mouseDown(org.sikuli.script.Mouse.LEFT);
            try {
                rest(0.05, 0.09);
            } finally {
                s.getRobot().mouseUp(org.sikuli.script.Mouse.LEFT);
            }
        }
    }

    static void handDrag(int x1, int y1, int x2, int y2) throws Exception {
        glide(x1, y1);
        if (glides) rest(0.08, 0.2);
        s.getRobot().mouseDown(org.sikuli.script.Mouse.LEFT);
        try {
            rest(0.1, 0.2);
            glide(x2, y2);
            if (glides) rest(0.08, 0.2);
        } finally {
            s.getRobot().mouseUp(org.sikuli.script.Mouse.LEFT);
        }
    }

    static void handType(String text) throws Exception {
        for (int i = 0; i < text.length(); i++) {
            String k = text.substring(i, i + 1);
            rest(0.06, 0.18);
            s.keyDown(k);
            try {
                rest(0.04, 0.08);
            } finally {
                s.keyUp(k);
            }
        }
    }

    static void handWheel(int direction, int notches) throws Exception {
        for (int i = 0; i < notches; i++) {
            if (i > 0) rest(0.06, 0.15);
            s.wheel(direction, 1);
        }
    }
    // One key held down (down true) or let go, after the hand's gap: for a chord, such as the agent's Q+N, whose keys
    // overlap.
    static void handKey(String k, boolean down) throws Exception {
        rest(0.06, 0.18);
        if (down) s.keyDown(k);
        else s.keyUp(k);
    }
    // (end of the hand)
    static int fails, cols;
    static boolean record;

    // The sidebar on the 1920x1080 screen: its rows are 38 pixels high between y=TOP and y=BOTTOM; at the top level a
    // label starts at x=39 and a folder's triangle at x=30, 12 pixels further in for each level. The Run button's
    // label is dark when it is enabled and light grey when not; the window's title names the selected demo.
    static final int TOP = 62, BOTTOM = 1058, SIDE_X = 24, SIDE_W = 240, INDENT = 12, LEAF_X = 39, TRIANGLE_X = 30;
    static final int ROW = 38;
    static final int RUN_X = 22, RUN_Y = 22, RUN_W = 56, RUN_H = 26;
    static final int TITLE_X = 640, TITLE_Y = 18, TITLE_W = 640, TITLE_H = 34;
    static final String INTRO = "GTK Demo";
    // Step times: how long to stay on an item before the next one, for the few items whose page needs it; the rest of
    // the list goes at the speed of the screen. Images: 3 s (2026-11-17). -step=NAME:MS adds or changes one;
    // -nostep runs without them.
    static final java.util.Map<String, Integer> STEP_MS = new java.util.HashMap<>(java.util.Map.of("Images", 3000));
    static boolean noStep;
    // The tree as found on the quick-start image (recorded with -record and checked by eye).
    static final File REF = new File("host/demo/ref/gtk_demo_tree.txt");
    // The launcher's GTK Demo tile (the cube), cut from a capture of the launcher.
    static final File TILE = new File("host/demo/ref/gtk_tile.png");
    // The launcher's environment, for starting the GTK Demo as the launcher does (as in LoginTest.java).
    static final String LAUNCHER_ENV = "PATH=/proc/boot:/system/bin ALSA_CONFIG_PATH=/usr/etc/alsa/alsa.conf"
            + " PCI_BKWD_COMPAT_MODULE=/proc/boot/pci_bkwd_compat.so LD_LIBRARY_PATH=/proc/boot:/system/lib:/system/lib/dll"
            + " FONTCONFIG_PATH=/etc/fonts TZDIR=/usr/share/zoneinfo PCI_SLOG_MODULE=pci_slog2.so HOME=/data/home/root"
            + " TERMINFO=/usr/share/terminfo TERM=xterm-256color PCI_CAP_MODULE_DIR=/proc/boot MM_INIT=/usr/lib/dll/mmedia"
            + " OMXIL_COMPONENT_PATH=/usr/lib/dll/omxil PCI_HW_CONFIG_FILE=/proc/boot/pci_hw.cfg VIMRUNTIME=/usr/share/vim/vim91";

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

    static void check(String name, boolean ok, String detail) {
        if (!ok) fails++;
        say(ok ? "PASS " : "FAIL ", name);
        if (!detail.isEmpty()) say("     ", detail);
    }

    static boolean dark(BufferedImage img, int x, int y) {
        int c = img.getRGB(x, y);
        return ((c >> 16) & 255) + ((c >> 8) & 255) + (c & 255) < 300;
    }

    static boolean darkLine(BufferedImage img, int y) {
        for (int x = SIDE_X; x < SIDE_W; x++)
            if (dark(img, x, y)) return true;
        return false;
    }

    // The middles of the rows wholly on the screen: each row's text is a band of lines with dark pixels.
    static List<Integer> rows(BufferedImage img) {
        List<Integer> out = new ArrayList<>();
        int y = TOP;
        while (y < BOTTOM) {
            if (!darkLine(img, y)) {
                y++;
                continue;
            }
            int start = y;
            while (y < BOTTOM && darkLine(img, y)) y++;
            int mid = (start + y) / 2;
            if (start > TOP + 2 && y < BOTTOM - 2 && mid - ROW / 2 >= TOP - 4 && mid + ROW / 2 <= BOTTOM + 4) out.add(mid);
        }
        return out;
    }

    // The leftmost dark pixel of a row, or -1.
    static int leftmost(BufferedImage img, int y) {
        for (int x = SIDE_X; x < SIDE_W; x++)
            for (int dy = -9; dy <= 9; dy++)
                if (dark(img, x, y + dy)) return x;
        return -1;
    }

    // A folder's triangle at x: {width, height}, or null. A triangle is 5x10 (closed, pointing right) or 10x5 (open,
    // pointing down) and stands where a triangle goes at some level; a capital letter can be as small, but starts
    // where a label goes.
    static int[] triangle(BufferedImage img, int x0, int y) {
        int minX = 9999, maxX = -1, minY = 9999, maxY = -1;
        for (int x = x0; x < x0 + 12; x++)
            for (int dy = -9; dy <= 9; dy++)
                if (dark(img, x, y + dy)) {
                    minX = Math.min(minX, x); maxX = Math.max(maxX, x);
                    minY = Math.min(minY, y + dy); maxY = Math.max(maxY, y + dy);
                }
        if (maxX < 0) return null;
        int w = maxX - minX + 1, h = maxY - minY + 1;
        boolean shape = (Math.abs(w - 5) <= 1 && Math.abs(h - 10) <= 1) || (Math.abs(w - 10) <= 1 && Math.abs(h - 5) <= 1);
        int off = Math.floorMod(minX - TRIANGLE_X + (w > h ? 0 : -2), INDENT); // the closed one starts 2 pixels in
        if (!shape || (off > 1 && off < INDENT - 1)) return null;
        for (int x = maxX + 1; x < maxX + 4; x++) // a gap before the label
            for (int dy = -9; dy <= 9; dy++)
                if (dark(img, x, y + dy)) return null;
        return new int[] { w, h };
    }

    static boolean runEnabled(BufferedImage img) {
        for (int y = RUN_Y; y < RUN_Y + RUN_H; y++)
            for (int x = RUN_X; x < RUN_X + RUN_W; x++)
                if (dark(img, x, y)) return true;
        return false;
    }

    // The GTK Demo's window is on the screen: the sidebar's light background shows right of the labels.
    static boolean onScreen() {
        BufferedImage now = s.capture().getImage();
        for (int y : new int[] { 300, 700, 1000 }) {
            int c = now.getRGB(244, y);
            if (((c >> 16) & 255) < 200 || ((c >> 8) & 255) < 200 || (c & 255) < 200) return false;
        }
        return true;
    }

    static String label(int x, int y) {
        return s.newRegion(x, y - 15, SIDE_W - x + 10, 30).text().replaceAll("\\s+", " ").trim();
    }

    // The dark pixels of a row's text, as offsets from its middle: what the row looks like whether or not it is
    // selected or under the pointer.
    static boolean[][] mask(BufferedImage img, int y) {
        return mask(img, y, SIDE_X);
    }

    // From x0 on: a folder's label without its triangle, which turns.
    static boolean[][] mask(BufferedImage img, int y, int x0) {
        boolean[][] m = new boolean[ROW][SIDE_W - SIDE_X];
        for (int dy = 0; dy < ROW; dy++) {
            int yy = y - ROW / 2 + dy;
            if (yy < 0 || yy >= img.getHeight()) continue;
            int bg = sum(img.getRGB(SIDE_W - 4, yy)); // the row's background, right of the labels
            for (int x = x0; x < SIDE_W - 4; x++)
                m[dy][x - SIDE_X] = bg - sum(img.getRGB(x, yy)) > 400;
        }
        return m;
    }

    // Text against the row's own background: the row's background changes when it is selected or under the pointer,
    // and a letter's smoothed edges with it, so a fixed darkness does not find the same pixels.
    static int sum(int c) {
        return ((c >> 16) & 255) + ((c >> 8) & 255) + (c & 255);
    }

    // Whether the row at y has the same text as the mask, allowing the text to sit up to 2 pixels higher or lower and
    // 2% of its pixels to differ (the row's middle is measured from its text, after a scroll it can move a pixel).
    static boolean sameRow(boolean[][] want, BufferedImage img, int y) {
        return sameRow(want, img, y, SIDE_X);
    }

    static boolean sameRow(boolean[][] want, BufferedImage img, int y, int x0) {
        int count = 0;
        for (boolean[] line : want)
            for (boolean b : line) if (b) count++;
        for (int dy = -2; dy <= 2; dy++) {
            boolean[][] got = mask(img, y + dy, x0);
            int differ = 0;
            for (int i = 0; i < want.length; i++)
                for (int j = 0; j < want[i].length; j++)
                    if (want[i][j] != got[i][j]) differ++;
            if (count > 0 && differ <= Math.max(3, count / 5)) return true; // the same text: 7 to 14%; another: 80%
        }
        return false;
    }

    // Captures until the condition holds on two captures 200 ms apart (the redraw has finished), for at most secs
    // seconds; returns the last capture, or null.
    static BufferedImage await(java.util.function.Predicate<BufferedImage> ok, double secs) throws Exception {
        long end = System.nanoTime() + (long) (secs * 1e9);
        do {
            BufferedImage now = s.capture().getImage();
            if (ok.test(now)) {
                Thread.sleep(200);
                BufferedImage again = s.capture().getImage();
                if (ok.test(again)) return again;
            }
            Thread.sleep(100);
        } while (System.nanoTime() < end);
        return null;
    }

    // Captures until two captures 300 ms apart are the same, for at most secs seconds.
    static BufferedImage still(double secs) throws Exception {
        long end = System.nanoTime() + (long) (secs * 1e9);
        BufferedImage a = s.capture().getImage();
        do {
            Thread.sleep(300);
            BufferedImage b = s.capture().getImage();
            if (same(a, b, 0, TOP, SIDE_W + 10, BOTTOM - TOP)) return b;
            a = b;
        } while (System.nanoTime() < end);
        return a;
    }

    static boolean same(BufferedImage a, BufferedImage b, int x0, int y0, int w, int h) {
        for (int y = y0; y < y0 + h; y++)
            for (int x = x0; x < x0 + w; x++)
                if (a.getRGB(x, y) != b.getRGB(x, y)) return false;
        return true;
    }

    // Clicks a folder's triangle; returns the screen once the triangle points down (open) or right (closed), or null
    // if it does not within 3 s.
    static BufferedImage toggle(int x, int y, int[] tri, boolean open) throws Exception {
        // Two clicks on a row within the double-click time run that demo (it happened in runs of 2026-11-17): wait
        // longer than that after the last click.
        long wait = lastClick + 800 - System.currentTimeMillis();
        if (wait > 0) Thread.sleep(wait);
        BufferedImage before = s.capture().getImage();
        boolean[][] name = mask(before, y, x + 14);
        click(s.newLocation(x + tri[0] / 2, y));
        lastClick = System.currentTimeMillis(); // after the press: the glide and rest before it take up to 1 s
        // Near the end of the list, closing a folder makes the list shorter and the rows move down: find the folder
        // again by its label.
        BufferedImage r = awaitFolder(name, x, open);
        if (r == null) { // what the screen showed, and how each row compared, for the failure
            BufferedImage now = s.capture().getImage();
            javax.imageio.ImageIO.write(before, "png", new File("demo-out", "gtk_folder_before.png"));
            javax.imageio.ImageIO.write(now, "png", new File("demo-out", "gtk_folder_after.png"));
            StringBuilder sb = new StringBuilder("y=" + y + " x=" + x + " rows:");
            for (int ry : rows(now)) {
                int[] t = triangle(now, x, ry);
                sb.append(" ").append(ry).append(sameRow(name, now, ry, x + 14) ? "=" : "").append(t == null ? "" : "t" + t[0] + "x" + t[1]);
            }
            say("     ", sb.toString());
        }
        return r;
    }

    static BufferedImage awaitFolder(boolean[][] name, int x, boolean open) throws Exception {
        return await(now -> {
            for (int ry : rows(now)) {
                if (!sameRow(name, now, ry, x + 14)) continue;
                int[] t = triangle(now, x, ry);
                if (t != null && (open ? t[0] > t[1] : t[0] < t[1])) {
                    toggledY = ry;
                    return true;
                }
            }
            return false;
        }, 3);
    }

    static int toggledY;

    // A click by hand on a point, or on something found by image: a hand aims at that, not at its exact centre
    // (within a quarter of its size).
    static void click(Object target) throws Exception {
        if (target instanceof Match) {
            Match m = (Match) target;
            org.sikuli.script.Location c = m.getTarget();
            handClick(c.x + (int) ((hand.nextDouble() - 0.5) * m.w / 2), c.y + (int) ((hand.nextDouble() - 0.5) * m.h / 2));
        } else {
            org.sikuli.script.Location l = (org.sikuli.script.Location) target;
            handClick(l.x, l.y);
        }
    }

    // Moves the pointer off the sidebar's rows (to the page on the right), so that no row is drawn as under it.
    static void pointerAway() throws Exception {
        glide(1500, 600);
    }

    // Scrolls the sidebar by `notches` of the mouse wheel, towards the end of the list when positive (the pointer over
    // the sidebar while it turns). In the GTK Demo on this image the wheel turned up (away from the user) moves the
    // list towards its end, with the real mouse as well (2026-11-17); the agent sends the values the real mouse does.
    static BufferedImage wheel(int notches) throws Exception {
        glide(SIDE_W - 60, (TOP + BOTTOM) / 2);
        handWheel(notches > 0 ? Button.WHEEL_UP : Button.WHEEL_DOWN, Math.abs(notches));
        pointerAway();
        return still(3);
    }

    // Runs commands on the target over SSH (host/tools/pi_ssh.sh, which reads ON_LOGIN_PASSWORD); returns the output.
    static String ssh(String commands) throws Exception {
        Process p = pi_ssh(commands).start();
        String out = new String(p.getInputStream().readAllBytes()).trim();
        p.waitFor();
        return out;
    }

    static String first(List<String> lines) {
        List<String> names = new ArrayList<>();
        for (String l : lines.subList(0, Math.min(5, lines.size()))) names.add(l.replaceAll("^\\s*\\[[a-z ]+\\]\\s*", ""));
        return lines.isEmpty() ? "none" : String.join(", ", names) + (lines.size() > 5 ? " and " + (lines.size() - 5) + " more" : "");
    }

    public static void main(String[] a) {
        int code;
        try {
            run(a);
            code = fails;
        } catch (Throwable e) {
            say("FAIL ", "the test stopped on an error: " + e);
            code = fails + 1;
        }
        try {
            ssh("rs slay -f gtk4-demo >/dev/null 2>&1"); // close the GTK Demo; the launcher comes back
        } catch (Throwable ignored) {
        }
        try {
            if (s != null) s.stop();
        } catch (Throwable ignored) {
        }
        System.out.println();
        System.out.println(code == 0 ? "ALL PASSED" : code + " FAILED");
        System.exit(code);
    }

    static long lastClick;
    static List<String> tree = new ArrayList<>();
    static int items, folders, runnable, notRunnable;

    static void run(String[] a) throws Exception {
        for (String x : a) if (x.startsWith("-cols=")) cols = Integer.parseInt(x.substring(6));
        record = java.util.Arrays.asList(a).contains("-record");
        noStep = java.util.Arrays.asList(a).contains("-nostep");
        for (String x : a)
            if (x.startsWith("-step=") && x.lastIndexOf(':') > 6)
                STEP_MS.put(x.substring(6, x.lastIndexOf(':')), Integer.parseInt(x.substring(x.lastIndexOf(':') + 1)));
        Object p = Class.forName("com.tigervnc.rfb.SecurityClient").getField("secTypes").get(null);
        p.getClass().getMethod("setParam", String.class).invoke(p, "VncAuth,None");

        caption("Connecting to the QNX 8.0 target through the OpticNerve agent");
        sshFallback = a[0].equals("-") ? null : a[0];
        s = VNCScreen.start(target(a[0]), vncPort(), vncPassword(a[1]), 10, 1000);
        boolean framed = s != null && s.capture() != null;
        check("connect to the OpticNerve agent", framed, target(a[0]) + ":" + vncPort());
        if (!framed) return;

        caption("Opening the GTK Demo from the launcher: finding its tile by image and clicking it");
        ssh("rs slay -f gtk4-demo >/dev/null 2>&1; sleep 3"); // start at the launcher, whatever ran before
        Match tile = s.exists(new Pattern(TILE.getAbsolutePath()).similar(0.8), 10);
        check("find the GTK Demo tile on the launcher", tile != null,
                tile == null ? "no match with " + TILE.getName() : String.format("score %.4f at %d,%d", tile.getScore(), tile.getCenter().x, tile.getCenter().y));
        if (tile == null) return;
        BufferedImage shown = null;
        for (int attempt = 1; attempt <= 2 && shown == null; attempt++) {
            click(tile);
            pointerAway();
            shown = await(now -> onScreen(), 10);
            say("     ", "click " + attempt + " on the tile: " + (shown != null ? "the GTK Demo is shown"
                    : "not shown after 10 s; gtk4-demo processes: " + ssh("rs pidin ar 2>/dev/null | grep -c '[g]tk4-demo'")));
        }
        check("the GTK Demo opens from its tile", shown != null, shown != null ? "" : "two clicks on the tile, no GTK Demo");
        if (shown == null) { // the rest of the test still runs: start it as the launcher does
            say("     ", "starting the GTK Demo over SSH, as the launcher does (/usr/bin/startGTK4Demo.sh)");
            ssh("cd /; rs env -i " + LAUNCHER_ENV + " sh -c 'nohup /usr/bin/startGTK4Demo.sh >/dev/null 2>&1 </dev/null &'");
            shown = await(now -> onScreen(), 20);
        }
        String runText = "";
        for (long end = System.nanoTime() + 10_000_000_000L; shown != null && !runText.contains("Run") && System.nanoTime() < end; ) {
            Thread.sleep(300); // the window draws its header bar after the sidebar
            runText = s.newRegion(RUN_X - 2, RUN_Y - 2, RUN_W + 4, RUN_H + 4).text().trim();
        }
        check("the GTK Demo is on the screen", shown != null && runText.contains("Run"), "OCR read \"" + runText + "\" on the Run button");
        if (shown == null || !runText.contains("Run")) return;

        caption("Scrolling the GTK Demo's sidebar to the top");
        BufferedImage img = wheel(-60);

        caption("Clicking each item; a folder's triangle twice (closed, then open again)");
        File shots = new File("demo-out");
        shots.mkdirs();
        int notchPx = 0; // how far one notch scrolls, measured on the first scroll
        int start = 0; // the first row on the screen not yet done
        String openedFolder = null;
        int openedDepth = 0;
        for (int page = 0; page < 40; page++) {
            List<Integer> rs = rows(img);
            int lastY = -1;
            for (int i = start; i < rs.size(); i++) {
                int y = rs.get(i);
                if (!onScreen()) {
                    check("the GTK Demo stays on the screen", false, "it is no longer shown, before item " + (items + 1));
                    report();
                    return;
                }
                int x = leftmost(img, y);
                if (x < 0) continue;
                int[] tri = triangle(img, x, y);
                int depth = Math.max(0, Math.round((x - (tri != null ? TRIANGLE_X : LEAF_X)) / (float) INDENT));
                String name = label(tri != null ? x + 14 : x - 2, y);
                String pad = "  ".repeat(depth);
                if (openedFolder != null) { // the item after a folder is its first item, a level further in
                    check(openedFolder + " (folder) shows its items", depth == openedDepth + 1,
                            depth == openedDepth + 1 ? "first item: " + name : "the next item is not inside it: " + name);
                    openedFolder = null;
                }
                items++;
                if (tri != null) {
                    folders++;
                    // Select the folder by its label: Run must be disabled (a folder runs nothing).
                    click(s.newLocation(x + 40, y));
                    lastClick = System.currentTimeMillis();
                    BufferedImage disabled = await(now -> !runEnabled(now), 1.5);
                    check(pad + name + " (folder) is not runnable", disabled != null,
                            disabled != null ? "" : "the Run button is enabled");
                    if (disabled != null) img = disabled;
                    String how = "opened";
                    if (tri[0] > tri[1]) { // open (the GTK Demo starts with its folders open): close it first
                        BufferedImage got = toggle(x, y, tri, false);
                        check(pad + name + " (folder) closes when its triangle is clicked", got != null,
                                got != null ? "the triangle turned to point right" : "the triangle did not turn");
                        if (got == null) {
                            report();
                            return;
                        }
                        img = got;
                        y = toggledY;
                        tri = triangle(img, x, y);
                        how = "closed and opened again";
                    }
                    BufferedImage got = toggle(x, y, tri, true);
                    check(pad + name + " (folder) opens when its triangle is clicked", got != null,
                            got != null ? "the triangle turned to point down" : "the triangle did not turn");
                    if (got == null) {
                        report();
                        return;
                    }
                    img = got;
                    y = toggledY;
                    rs = rows(img); // its items are on the screen below it now
                    i = rs.indexOf(y) >= 0 ? rs.indexOf(y) : i;
                    openedFolder = pad + name;
                    openedDepth = depth;
                    tree.add(pad + "[folder] " + name + " (" + how + ")");
                } else {
                    // Click the label; the window's title changes to the demo's name, then the Run button follows.
                    BufferedImage before = img;
                    click(s.newLocation(x + 30, y));
                    lastClick = System.currentTimeMillis();
                    BufferedImage got = await(now -> !same(before, now, TITLE_X, TITLE_Y, TITLE_W, TITLE_H), 1.5);
                    img = await(GtkDemoTest::runEnabled, got == null ? 0.3 : 1);
                    boolean ok = img != null;
                    if (img == null) img = s.capture().getImage();
                    // The first item is the GTK Demo's introduction: neither a folder nor a demo, nothing to run.
                    boolean intro = items == 1 && name.equals(INTRO);
                    if (intro) {
                        check(name + " (the introduction) is not runnable", !ok, ok ? "the Run button is enabled" : "");
                    } else {
                        if (ok) runnable++;
                        else notRunnable++;
                        check(pad + name + " is runnable", ok, ok ? "" : "the Run button is disabled");
                    }
                    tree.add(pad + (intro && !ok ? "[intro]  " : ok ? "[run]    " : "[no run] ") + name);
                    Integer step = noStep ? null : STEP_MS.get(name);
                    if (step != null) {
                        say("     ", "step time: " + step + " ms on " + name);
                        Thread.sleep(step);
                    }
                }
                lastY = y;
            }
            if (lastY < 0) break;

            // The next page: scroll, then find the last row done (its text's pixels) and start below it.
            pointerAway();
            img = still(2);
            boolean[][] last = mask(img, lastY);
            int notches = notchPx == 0 ? 1 : Math.max(1, (int) ((lastY - TOP - ROW) / (double) notchPx));
            BufferedImage moved = wheel(notches);
            if (same(img, moved, 0, TOP, SIDE_W, BOTTOM - TOP)) break; // the end of the list: nothing moved
            int found = -1;
            for (int back = 0; back < 3 && found < 0; back++) {
                List<Integer> now = rows(moved);
                for (int i = 0; i < now.size() && found < 0; i++)
                    if (sameRow(last, moved, now.get(i))) found = i;
                if (found < 0) moved = wheel(-1); // too far: back a notch
            }
            if (found < 0) {
                javax.imageio.ImageIO.write(moved, "png", new File(shots, "gtk_lost.png"));
                javax.imageio.ImageIO.write(img, "png", new File(shots, "gtk_before.png"));
                say("     ", "last row at y=" + lastY + ", " + notches + " notches of " + notchPx + " px; rows now " + rows(moved));
                check("the last item done is found again after scrolling", false,
                        "not on the screen; the screen is in demo-out" + File.separator + "gtk_lost.png");
                break;
            }
            int newY = rows(moved).get(found);
            if (notchPx == 0) notchPx = Math.max(1, lastY - newY);
            img = moved;
            start = found + 1;
            if (start >= rows(img).size()) break; // the last item was already the last one on the list
        }
        report();
    }

    static void report() throws Exception {
        caption("The GTK Demo's sidebar");
        File out = new File("demo-out", "gtk_demo_tree.txt");
        out.getParentFile().mkdirs();
        try (PrintWriter w = new PrintWriter(out, "UTF-8")) {
            for (String line : tree) {
                say("  | ", line);
                w.println(line);
            }
        }
        say("", items + " items: 1 introduction, " + folders + " folders, " + runnable + " runnable, " + notRunnable + " not runnable"
                + " (" + out.getPath() + ")");

        // The tree without the notes on how each folder was found, against the reference.
        List<String> found = new ArrayList<>();
        for (String line : tree) found.add(line.replaceAll(" \\(.*\\)$", "").stripTrailing());
        caption("Comparing the sidebar with " + REF.getPath());
        if (record) {
            java.nio.file.Files.write(REF.toPath(), found);
            say("", "recorded " + found.size() + " lines");
            return;
        }
        List<String> want = java.nio.file.Files.readAllLines(REF.toPath());
        List<String> missing = new ArrayList<>(want), extra = new ArrayList<>(found);
        missing.removeAll(found);
        extra.removeAll(want);
        boolean same = want.equals(found);
        check("the sidebar is as recorded", same, same ? want.size() + " items, in the same order"
                : found.size() + " items, " + want.size() + " recorded; missing " + first(missing) + "; new " + first(extra)
                        + (missing.isEmpty() && extra.isEmpty() ? "; the order differs" : ""));
    }
}
