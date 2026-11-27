// The OpticNerve demo test: OculiX drives the demo panel (agent/demo/demo_panel.c) on a QNX 8.0 target through
// the OpticNerve agent, and checks what the panel shows. Each step prints PASS or FAIL; the exit status is the
// number of failures.
//
//   java -cp "tigervnc-java-oculix-2.0.4.jar;oculixapi-4.0.0-windows.jar" host/demo/DemoTest.java HOST|- PASSWORD|- [OUTDIR] [-fail] [-slow] [-cols=N] [-brief] [-repeat=N] [-hand]
// A - for HOST or PASSWORD takes ON_HOST or ON_VNC_PASSWORD from the environment or the settings file
// (host/tools/opticnerve.env.example); SSH goes to ON_SSH_HOST:ON_SSH_PORT (default HOST, port 22).
//
// Needs the agent running on HOST (port ON_VNC_PORT) and demo_panel showing. -fail adds one check that expects the wrong
// count, to show what a caught failure looks like. Between steps the test asks the agent on the target, over SSH,
// to wait until the screen has settled (`opticnerve wait -quiet`), when ON_LOGIN_PASSWORD is set; otherwise it
// waits a fixed time. Configuration ON-VNC-1: the tigervnc-java-oculix jar first, security types VncAuth,None.
//
// Before each step the test prints a caption line ("==> ..."), for the video's captions. -slow paces the run for
// recording: a pause after each caption. -brief prints only the PASS and FAIL lines (no captions, no screenshots).
// Keys are typed the way a hand does (see the hand below). The pointer jumps straight to each target, unless -hand
// or -slow is given: then it glides the way a hand does, so the demo panel's pointer marker follows each move.
// -repeat=N runs the checks N times on one connection. Every run ends with its number of checks and its time.
import java.awt.Color;
import java.awt.image.BufferedImage;
import java.io.File;
import javax.imageio.ImageIO;
import org.sikuli.script.Match;
import org.sikuli.script.Pattern;
import org.sikuli.script.Region;
import org.sikuli.vnc.VNCScreen;

public class DemoTest {
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
    static int vncPort() { return Integer.parseInt(cfg("ON_VNC_PORT", "5901")); }

    static VNCScreen s;
    static File out;
    static int fails, n;

    static boolean slow, brief;
    static int checks;
    static int cols; // -cols=N: wrap output at word boundaries to N columns (for a large font in the video); 0: no wrap

    // Prints text after a first-line prefix; continuation lines are indented to the prefix's width.
    static void say(String prefix, String text) {
        if (cols <= prefix.length() + 10) {
            System.out.println(prefix + text);
            return;
        }
        String indent = " ".repeat(prefix.length());
        StringBuilder line = new StringBuilder(prefix);
        int start = prefix.length();
        for (String w : text.split(" ")) {
            if (line.length() > start && line.length() + 1 + w.length() > cols) {
                System.out.println(line);
                line = new StringBuilder(indent);
            }
            if (line.length() > start && line.charAt(line.length() - 1) != ' ') line.append(' ');
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

    static void caption(String text) throws Exception {
        overlay(text);
        if (brief) return;
        System.out.println();
        say("==> ", text);
        if (slow) Thread.sleep(1500);
    }

    static void check(String name, boolean ok, String detail) {
        checks++;
        if (!ok) fails++;
        if (cols > 0) {
            say(ok ? "PASS " : "FAIL ", name);
            say("     ", detail);
        } else {
            System.out.printf("%-4s %-44s %s%n", ok ? "PASS" : "FAIL", name, detail);
        }
    }

    // The agent on the target says when the screen has settled; without SSH, a fixed pause. One SSH session (a shell
    // on the target) serves the whole test, so a wait costs the wait and not an SSH login.
    static String host;
    static Process ssh;
    static java.io.BufferedReader sshOut;
    static java.io.Writer sshIn;

    // The change count from the last wait: the next wait asks for a change past it (-since), so it cannot return on the
    // stillness before the panel has responded, and then for a short quiet time (100 ms in a -brief run, as the panel
    // draws each response in one frame; 300 ms otherwise). With the pointer marker on, a wait can return while the
    // ring is still on the clicked button: the panel takes it away 1 s after the pointer rests, and the next image
    // search (s.wait, up to 3 s) finds the button then.
    static long seen = -1;

    static void settle() throws Exception {
        if (cfg("ON_LOGIN_PASSWORD", null) == null) {
            Thread.sleep(700);
            return;
        }
        long t0 = System.nanoTime();
        String said = agent("wait -since=" + seen + " -quiet=" + (brief ? 100 : 300) + " -timeout=3");
        if (!brief) System.out.printf("     opticnerve wait: %s (%.1f s)%n", said, (System.nanoTime() - t0) / 1e9);
    }

    // Before the first action: the change count now.
    static void startCount() throws Exception {
        if (cfg("ON_LOGIN_PASSWORD", null) != null) agent("wait -since=0 -timeout=1");
    }

    // Runs `opticnerve ARGS` on the target and returns its output; notes the change count it reports.
    static String agent(String args) throws Exception {
        if (ssh == null) {
            String askpass = new File("host/tools/pi_askpass.sh").getAbsolutePath().replace('\\', '/');
            // Git's ssh through bash (askpass is a shell script); no quotes inside the bash command, since Java on
            // Windows does not pass embedded quotes through intact
            String user = cfg("ON_LOGIN_USER", "qnxuser");
            // SSH goes to ON_SSH_HOST:ON_SSH_PORT (default: the VNC address, port 22)
            String sshHost = cfg("ON_SSH_HOST", host), sshPort = cfg("ON_SSH_PORT", "22");
            ProcessBuilder pb = new ProcessBuilder("bash", "-c", "exec ssh $@", "ssh", "-q", "-o", "PubkeyAuthentication=no",
                    "-o", "ConnectTimeout=10", "-o", "StrictHostKeyChecking=accept-new", "-p", sshPort, user + "@" + sshHost, "sh -s")
                    .redirectErrorStream(true);
            pb.environment().put("SSH_ASKPASS", askpass);
            pb.environment().put("SSH_ASKPASS_REQUIRE", "force");
            pb.environment().put("DISPLAY", "x");
            pb.environment().put("ON_LOGIN_PASSWORD", cfg("ON_LOGIN_PASSWORD", "")); // for pi_askpass.sh
            ssh = pb.start();
            sshOut = new java.io.BufferedReader(new java.io.InputStreamReader(ssh.getInputStream()));
            sshIn = new java.io.OutputStreamWriter(ssh.getOutputStream());
        }
        sshIn.write("/data/opticnerve/bin/opticnerve " + args + " 2>&1; echo '#end'\n");
        sshIn.flush();
        StringBuilder said = new StringBuilder();
        for (String l; (l = sshOut.readLine()) != null && !l.equals("#end"); ) said.append(l.trim());
        java.util.regex.Matcher m = java.util.regex.Pattern.compile("^(change|quiet|timeout) (\\d+)$").matcher(said);
        if (m.matches()) seen = Long.parseLong(m.group(2));
        return said.toString();
    }

    static void shot(String name) throws Exception {
        if (brief) return; // screenshots take time; a brief run is about speed
        ImageIO.write(s.capture().getImage(), "png", new File(out, String.format("%02d_%s.png", ++n, name)));
    }

    // A pattern cut from OculiX's own capture: the button as it is on the screen right now.
    static Pattern cut(int x, int y, int w, int h, String name) throws Exception {
        BufferedImage img = s.capture(x, y, w, h).getImage();
        File f = new File(out, "pattern_" + name + ".png");
        ImageIO.write(img, "png", f);
        return new Pattern(f.getAbsolutePath()).similar(0.9); // OculiX resolves a relative path in its own image folder
    }

    // OCR of a region with OculiX. The panel's text is anti-aliased Roboto, which Tesseract reads as drawn (the typed
    // text keeps its case).
    static String read(int x, int y, int w, int h) {
        Region r = s.newRegion(x, y, w, h);
        return r.text().replaceAll("\\s+", " ").trim();
    }

    // The count is checked by image: the count box must match a reference picture of that number (host/demo/ref/),
    // recorded once with -record and checked by eye. OCR guessed wrongly at the old panel's single blocky digits, and
    // the agent's frames are exact, so a reference picture is the reliable check. -record saves instead of checking.
    static boolean record;

    static String countIs(int n) throws Exception {
        File ref = new File("host/demo/ref", "count_" + n + ".png").getAbsoluteFile();
        if (record) {
            ref.getParentFile().mkdirs();
            ImageIO.write(s.capture(160, 280, 700, 180).getImage(), "png", ref);
            return "recorded " + ref.getPath();
        }
        // Pixel by pixel: the box is mostly flat background, which template matching scores poorly (tried: no match
        // even against a picture recorded from the same screen), and the agent's frames are exact.
        // For up to 1 s: the check can come before the panel's redraw (a take of 2026-11-17 failed so).
        BufferedImage want = ImageIO.read(ref);
        long end = System.nanoTime() + 1_000_000_000L;
        do {
            BufferedImage got = s.capture(160, 280, 700, 180).getImage();
            int differ = 0;
            for (int y = 0; y < want.getHeight(); y++)
                for (int x = 0; x < want.getWidth(); x++)
                    if ((want.getRGB(x, y) & 0xffffff) != (got.getRGB(x, y) & 0xffffff)) differ++;
            if (differ == 0) return "equals " + ref.getName() + " (0 pixels differ)";
            Thread.sleep(50);
        } while (System.nanoTime() < end);
        return null;
    }

    // The demo panel's title bar colour at (1500, 30), right of its title (agent/demo/demo_panel.c).
    static final int TITLE_BAR = 0x0a3d62;

    // Why img is not the demo panel, or null if it is: 1920 x 1080, the title bar's colour, the lamp red or green.
    static String notPanel(BufferedImage img) {
        if (img.getWidth() != 1920 || img.getHeight() != 1080)
            return "the frame is " + img.getWidth() + "x" + img.getHeight() + ", not 1920x1080";
        int t = img.getRGB(1500, 30) & 0xffffff, l = img.getRGB(1190, 370) & 0xffffff;
        if (t != TITLE_BAR)
            return String.format("the title bar is #%06x, not #%06x", t, TITLE_BAR);
        int r = l >> 16, g = (l >> 8) & 0xff;
        if (!(r > 200 && g < 100) && !(g > 130 && r < 100))
            return String.format("the lamp is #%06x, neither red nor green", l);
        return null;
    }

    // Polls captures for up to secs seconds until the demo panel is on the screen; the last reason if it never is.
    static String waitForPanel(int secs) throws Exception {
        String why = "no frame";
        for (long end = System.nanoTime() + secs * 1_000_000_000L; System.nanoTime() < end; Thread.sleep(250)) {
            why = notPanel(s.capture().getImage());
            if (why == null)
                return null;
        }
        return "not on the screen after " + secs + " s: " + why;
    }

    static String lamp() {
        Color c = s.newLocation(1190, 370).getColor();
        if (c.getRed() > 200 && c.getGreen() < 100) return "RED";
        if (c.getGreen() > 130 && c.getRed() < 100) return "GREEN";
        return c.toString();
    }

    // Any error ends the run as a failure: OculiX's VNC threads would otherwise keep the program alive (a take of
    // 2026-11-16 hung after a pattern was not found).
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
            if (ssh != null) ssh.destroy();
            if (s != null) s.stop();
        } catch (Throwable ignored) {
        }
        System.out.println();
        System.out.println(code == 0 ? "ALL PASSED" : code + " FAILED");
        System.exit(code);
    }

    static void run(String[] a) throws Exception {
        host = target(a[0]);
        boolean showFailure = java.util.Arrays.asList(a).contains("-fail");
        record = java.util.Arrays.asList(a).contains("-record");
        slow = java.util.Arrays.asList(a).contains("-slow");
        brief = java.util.Arrays.asList(a).contains("-brief");
        glides = slow || java.util.Arrays.asList(a).contains("-hand");
        int repeat = 1;
        for (String x : a) if (x.startsWith("-repeat=")) repeat = Integer.parseInt(x.substring(8));
        for (String x : a) if (x.startsWith("-cols=")) cols = Integer.parseInt(x.substring(6));
        out = new File(a.length > 2 && !a[2].startsWith("-") ? a[2] : "demo-out").getAbsoluteFile();
        out.mkdirs();
        Object p = Class.forName("com.tigervnc.rfb.SecurityClient").getField("secTypes").get(null);
        p.getClass().getMethod("setParam", String.class).invoke(p, "VncAuth,None");
        caption("Connecting to the QNX 8.0 target through the OpticNerve agent (VNC, port " + vncPort() + ")");
        s = VNCScreen.start(target(a[0]), vncPort(), vncPassword(a[1]), 10, 1000);
        boolean framed = s != null && s.capture() != null; // a frame arrived, not just an object returned
        check("connect to the OpticNerve agent", framed, target(a[0]) + ":" + vncPort() + (framed ? "" : ", no frame"));
        if (!framed) return;
        // Before any pattern is cut from the screen: the demo panel must be on it. A run that started on an all-black
        // frame once cut its patterns from black and "found" RESET at 1.0000 (#505).
        BufferedImage black = new BufferedImage(1920, 1080, BufferedImage.TYPE_INT_RGB);   // all black, as in #505
        String b = notPanel(black), small = notPanel(new BufferedImage(1280, 768, BufferedImage.TYPE_INT_RGB));
        check("an all-black or wrong-size frame is not the panel", b != null && small != null,
            "black: " + b + "; 1280x768: " + small);
        String why = waitForPanel(30);
        check("the demo panel is on the screen", why == null, why == null ? "1920x1080, title bar and lamp seen" : why);
        if (why != null) {
            ImageIO.write(s.capture().getImage(), "png", new File(out, "not_on_screen.png"));
            return;
        }
        shot("start");

        plus = cut(160, 700, 420, 200, "plus");
        reset = cut(750, 700, 420, 200, "reset");
        lampButton = cut(1340, 700, 420, 200, "lamp");
        startCount();
        int allChecks = 0, allFails = 0;
        double total = 0;
        for (int i = 1; i <= repeat; i++) {
            checks = 0;
            int failsBefore = fails;
            long t0 = System.nanoTime();
            checks(showFailure);
            double secs = (System.nanoTime() - t0) / 1e9;
            total += secs;
            allChecks += checks;
            allFails += fails - failsBefore;
            if (!brief) System.out.println();
            System.out.printf("%s%d checks, %d failed, %.1f s%n", repeat > 1 ? "run " + i + ": " : "", checks, fails - failsBefore, secs);
        }
        if (repeat > 1)
            System.out.printf("%n%d runs, %d checks, %d failed, %.1f s per run%n", repeat, allChecks, allFails, total / repeat);
    }

    static Pattern plus, reset, lampButton;

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
    // (end of the hand)

    // A button found by image: a hand aims at it, not at its exact centre (within a quarter of its size).
    static void click(Pattern target) throws Exception {
        Match m = s.wait(target, 3);
        org.sikuli.script.Location c = m.getTarget();
        handClick(c.x + (int) ((hand.nextDouble() - 0.5) * m.w / 2), c.y + (int) ((hand.nextDouble() - 0.5) * m.h / 2));
    }

    static void checks(boolean showFailure) throws Exception {
        caption("Finding the RESET button by image, and clicking it");
        Match m = s.exists(reset, 3);
        check("find the RESET button by image", m != null, m == null ? "not found" : String.format("score %.4f", m.getScore()));
        click(reset);
        settle();
        caption("Checking the count shows 0, pixel by pixel");
        String r0 = countIs(0);
        check("after RESET the count shows 0", r0 != null, r0 == null ? "no match with count_0.png" : r0);

        caption("Clicking +1 three times; the agent on the target says when the screen has settled");
        for (int i = 1; i <= 3; i++) {
            click(plus);
            settle();
        }
        caption("Checking the count shows 3");
        String r3 = countIs(3);
        check("after three clicks on +1 the count shows 3", r3 != null, r3 == null ? "no match with count_3.png" : r3);
        shot("count3");

        caption("Clicking LAMP and checking the lamp turns from green (as RESET left it) to red");
        String before = lamp();
        click(lampButton);
        settle();
        String after = lamp();
        for (int i = 0; i < 20 && before.equals(after); i++) { // up to 1 s, as for the count
            Thread.sleep(50);
            after = lamp();
        }
        check("LAMP switches the lamp from green to red", before.equals("GREEN") && after.equals("RED"), before + " -> " + after);
        shot("lamp");

        caption("Typing on the target's keyboard input, and reading the text back with OCR");
        handType("\r");  // Return clears the text line (Enter as "\r", ON-VNC-1)
        settle();
        handType("hello pi 5");
        settle();
        String typed = read(400, 505, 1360, 80);
        for (long end = System.nanoTime() + 1_000_000_000L; !typed.equals("hello pi 5") && System.nanoTime() < end; )
            typed = read(400, 505, 1360, 80); // up to 1 s, as for the count
        check("typed text appears on the panel", typed.equals("hello pi 5"), "OCR read \"" + typed + "\"");
        shot("typed");

        if (showFailure && !record) {
            caption("A check that expects the wrong count, to show a failure being caught");
            String wrong = countIs(0);
            check("(deliberate) the count still shows 0", wrong != null, wrong == null ? "no match with count_0.png" : wrong);
        }

        // A run leaves the panel as it found it, so runs can follow one another (-repeat, or a loop of runs)
        caption("Leaving the panel as it started: RESET, then count 0, lamp green, text line empty");
        click(reset);
        settle();
        String c0 = countIs(0);
        String lampNow = lamp();
        String left = read(400, 505, 1360, 80);
        check("the run leaves the panel as it started", c0 != null && lampNow.equals("GREEN") && left.isEmpty(),
            "count " + (c0 == null ? "not 0" : "0") + ", lamp " + lampNow + ", text \"" + left + "\"");
    }
}
