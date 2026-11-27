// A test of the QNX 8.0 quick-start image's Developer Desktop (Weston with Xfce), driven by OculiX through the
// OpticNerve agent: start the desktop (over SSH, as the launcher starts it: the launcher does not act on injected
// input), open the web browser from the Applications menu, go to the OpticNerve repository on GitHub, show its banner
// full screen, close the desktop. Each step prints PASS or FAIL; the exit status is the number of failures.
//
//   java -cp "tigervnc-java-oculix-2.0.4.jar;oculixapi-4.0.0-windows.jar" host/demo/DesktopTest.java HOST|- VNCPASSWORD|- [-hold=S] [-cols=N]
// A - for HOST or PASSWORD takes ON_HOST or ON_VNC_PASSWORD from the environment or the settings file
// (host/tools/opticnerve.env.example); SSH goes to ON_SSH_HOST:ON_SSH_PORT (default HOST, port 22).
//
// Needs the agent running on HOST (port ON_VNC_PORT), ON_LOGIN_PASSWORD set (for SSH) and the target online. -hold=S keeps
// the banner on the screen for S seconds before the desktop is closed (for the video).
import org.sikuli.script.Key;
import org.sikuli.vnc.VNCScreen;

public class DesktopTest {
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
    // (end of the hand)
    static int fails, cols;

    // No character that needs Shift: OculiX sends Shift as a key event of its own, and in the browser's address bar
    // it landed on the wrong character (2026-11-17, still to be investigated). GitHub ignores case in this path.
    static final String URL = "github.com/davidyoung8196504567-sudo/opticnerve";

    // The launcher's Developer Desktop entry runs this as root with the launcher's environment (as for the Terminal
    // in LoginTest.java). startDesktop.sh runs one desktop at a time.
    static final String START = "/usr/bin/sudo -i -u qnxuser /bin/bash -l -c /usr/bin/startDesktop.sh";
    static final String LAUNCHER_ENV = "PATH=/proc/boot:/system/bin ALSA_CONFIG_PATH=/usr/etc/alsa/alsa.conf"
            + " PCI_BKWD_COMPAT_MODULE=/proc/boot/pci_bkwd_compat.so LD_LIBRARY_PATH=/proc/boot:/system/lib:/system/lib/dll"
            + " FONTCONFIG_PATH=/etc/fonts TZDIR=/usr/share/zoneinfo PCI_SLOG_MODULE=pci_slog2.so HOME=/data/home/root"
            + " TERMINFO=/usr/share/terminfo TERM=xterm-256color PCI_CAP_MODULE_DIR=/proc/boot MM_INIT=/usr/lib/dll/mmedia"
            + " OMXIL_COMPONENT_PATH=/usr/lib/dll/omxil PCI_HW_CONFIG_FILE=/proc/boot/pci_hw.cfg VIMRUNTIME=/usr/share/vim/vim91";

    // Where things are on the 1920x1080 screen: the Applications menu in the desktop's panel, and its Web Browser item.
    static final int MENU_X = 55, MENU_Y = 12, BROWSER_X = 70, BROWSER_Y = 121, ADDRESS_X = 1030, ADDRESS_Y = 226;

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
        say("     ", detail);
    }

    static String read(int x, int y, int w, int h) {
        return s.newRegion(x, y, w, h).text().replaceAll("[ \t]+", " ").trim();
    }

    // Letters and digits only: OCR varies in punctuation and spacing.
    static boolean has(String screen, String line) {
        return fold(screen).contains(fold(line));
    }

    static String fold(String s) {
        return s.toLowerCase().replaceAll("[^a-z0-9]", "");
    }

    static String waitFor(String want, int x, int y, int w, int h, double secs) throws Exception {
        long end = System.nanoTime() + (long) (secs * 1e9);
        String t;
        do {
            t = read(x, y, w, h);
            if (has(t, want)) break;
            Thread.sleep(300);
        } while (System.nanoTime() < end);
        return t;
    }

    static String ssh(String commands) throws Exception {
        Process p = pi_ssh(commands).start();
        String out = new String(p.getInputStream().readAllBytes()).trim();
        p.waitFor();
        return out;
    }

    // The agent on the target says when the screen has settled (no change for MS milliseconds).
    static String settle(int ms, int timeout) throws Exception {
        return ssh("/data/opticnerve/bin/opticnerve wait -since=0 -quiet=" + ms + " -timeout=" + timeout);
    }

    static int differ(java.awt.image.BufferedImage a, java.awt.image.BufferedImage b) {
        int n = 0;
        for (int y = 0; y < a.getHeight(); y++)
            for (int x = 0; x < a.getWidth(); x++)
                if ((a.getRGB(x, y) & 0xffffff) != (b.getRGB(x, y) & 0xffffff)) n++;
        return n;
    }

    // What the screen showed when a check failed, in demo-out/.
    static void failShot(String name) throws Exception {
        java.io.File f = new java.io.File("demo-out", "desktop_fail_" + name + ".png");
        f.getParentFile().mkdirs();
        javax.imageio.ImageIO.write(s.capture().getImage(), "png", f);
        say("     ", "the screen is in " + f.getPath());
    }

    static String shortened(String t) {
        t = t.replace('\n', ' ');
        return t.length() > 70 ? t.substring(0, 70) + "..." : t;
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
            ssh("rs slay -f weston >/dev/null 2>&1"); // close the desktop; the launcher comes back
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

    static void run(String[] a) throws Exception {
        int hold = 0;
        for (String x : a) if (x.startsWith("-cols=")) cols = Integer.parseInt(x.substring(6));
        for (String x : a) if (x.startsWith("-hold=")) hold = Integer.parseInt(x.substring(6));
        Object p = Class.forName("com.tigervnc.rfb.SecurityClient").getField("secTypes").get(null);
        p.getClass().getMethod("setParam", String.class).invoke(p, "VncAuth,None");

        caption("Connecting to the QNX 8.0 target through the OpticNerve agent");
        sshFallback = a[0].equals("-") ? null : a[0];
        s = VNCScreen.start(target(a[0]), vncPort(), vncPassword(a[1]), 10, 1000);
        boolean framed = s != null && s.capture() != null;
        check("connect to the OpticNerve agent", framed, target(a[0]) + ":" + vncPort());
        if (!framed) return;

        caption("Starting the Developer Desktop on the target, as the launcher does (over SSH)");
        String started = ssh("cd /; rs slay -f weston >/dev/null 2>&1; sleep 1; rs env -i " + LAUNCHER_ENV
                + " sh -c 'nohup " + START + " >/dev/null 2>&1 </dev/null &'; sleep 1; echo started");
        String panel = started.contains("started") ? waitFor("Applications", 0, 0, 300, 28, 20) : "";
        check("the desktop shows its Applications menu", has(panel, "Applications"), "OCR read \"" + shortened(panel) + "\"");
        if (!has(panel, "Applications")) return;
        settle(1000, 10);

        caption("Opening the web browser from the Applications menu");
        handClick(MENU_X, MENU_Y);
        String menu = waitFor("Web Browser", 0, 28, 340, 320, 5);
        check("the menu lists the web browser", has(menu, "Web Browser"), "OCR read \"" + shortened(menu) + "\"");
        if (!has(menu, "Web Browser")) return;
        java.awt.image.BufferedImage before = s.capture(760, 440, 400, 200).getImage();
        handClick(BROWSER_X, BROWSER_Y);
        String running = "";
        for (int i = 0; i < 30 && !running.contains("epiphany"); i++) {
            Thread.sleep(500);
            running = ssh("rs pidin ar 2>/dev/null | grep -q '[e]piphany' && echo epiphany");
        }
        check("the web browser starts", running.contains("epiphany"), running.contains("epiphany") ? "epiphany is running" : "no epiphany process");
        if (!running.contains("epiphany")) return;
        // The window opens in the middle of the screen some seconds after the process starts.
        boolean shown = false;
        for (int i = 0; i < 40 && !shown; i++) {
            Thread.sleep(500);
            shown = differ(before, s.capture(760, 440, 400, 200).getImage()) > 20000;
        }
        check("the browser window opens", shown, shown ? "the middle of the screen changed" : "the middle of the screen did not change");
        if (!shown) return;
        settle(1500, 20);

        caption("Going to the OpticNerve repository on GitHub");
        // The window opens in the middle of the screen at the same size each time; a click in the address bar selects
        // its text, so typing replaces it.
        handClick(ADDRESS_X, ADDRESS_Y);
        Thread.sleep(300);
        handType(URL);
        settle(1000, 10); // the address bar's suggestions come first; Enter typed while they load was lost
        handType("\r"); // Return; Key.ENTER is a line feed, which the address bar ignores
        // "Public" is the badge beside the repository's name: on the page, not in the address bar's suggestions.
        String page = waitFor("Public", 0, 0, 1920, 1080, 30);
        boolean repo = has(page, "Public") && has(page, "davidyoung8196504567-sudo") && has(page, "OpticNerve");
        check("the browser shows the OpticNerve repository", repo,
                repo ? "OCR read the owner, the project name and Public" : "OCR read \"" + shortened(page) + "\"");
        if (!repo) failShot("repository");
        settle(1500, 20);

        caption("Full screen, and down to the README's banner");
        handType(Key.F11);
        settle(800, 10);
        handType(Key.PAGE_DOWN);
        settle(800, 10);
        String readme = waitFor("Gives your test tools eyes and hands", 0, 0, 1920, 1080, 10);
        check("the README is on the screen", has(readme, "Gives your test tools eyes and hands"),
                has(readme, "Gives your test tools eyes and hands") ? "OCR read \"Gives your test tools eyes and hands\"" : "OCR read \"" + shortened(readme) + "\"");
        if (!has(readme, "Gives your test tools eyes and hands")) failShot("readme");
        Thread.sleep(hold * 1000L);

        caption("Closing the desktop");
        ssh("rs slay -f weston >/dev/null 2>&1");
        String launcher = waitFor("Developer Desktop", 0, 0, 1920, 1080, 10);
        check("the launcher is back", has(launcher, "Developer Desktop"), "OCR read \"Developer Desktop\"");
    }
}
