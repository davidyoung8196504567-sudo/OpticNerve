// The OpticNerve launcher test: OculiX opens each tile of our launcher panel (agent/demo/launch_panel.c) on a QNX 8.0
// target through the OpticNerve agent, as a person in a viewer does, and closes the app again with the agent's key
// chord (hold Q, then N, then X: Alt+Tab+Del with Tab held, which closes the window selected in the window switcher).
// Each step prints PASS, FAIL or N/A; the exit status is the number of failures.
//
//   java -cp "tigervnc-java-oculix-2.0.4.jar;oculixapi-4.0.0-windows.jar" host/demo/LauncherTest.java HOST|- PASSWORD|- [OUTDIR] [-gl] [-only=NAME,...] [-hand]
// A - for HOST or PASSWORD takes ON_HOST or ON_VNC_PASSWORD from the environment or the settings file
// (host/tools/opticnerve.env.example); SSH goes to ON_SSH_HOST:ON_SSH_PORT (default HOST, port 22).
//
// Needs the agent running on HOST (port ON_VNC_PORT, key chord on), the launcher panel in front with no app of it
// running (host/tools/launch_panel.sh start), and ON_LOGIN_PASSWORD (SSH reads the panel's log and state, the core
// dumps, and closes what a failed step leaves open). Configuration ON-VNC-1: the tigervnc-java-oculix jar first.
//
// The tests (IDs in private/docs/DESIGN_LAUNCHER.md):
//   SU01 (connect to the agent), SU02 (the launcher panel in front with its tiles), SU03 (no app of the panel running)
//   LP13 (a tile opens its app, the key chord closes it): Terminal, Readme, OpticNerve Demo Panel, GTK4Demo, Widget
//        Factory, the tiles that need no OpenGL
//   LP14 (an OpenGL tile opens its app, the key chord closes it): Developer Desktop, Gears, Maze; only with -gl, and
//        N/A on a target without a GPU (/dev/dri/card0, which the QEMU guest's virtual display does not have)
//   TD01 (the panel left as it was found: in front, nothing of it running, no new core dump)
// For each tile: a click on it starts the app (the panel's state lists it, and the panel is no longer in front); the
// chord, aimed at the app's thumbnail in the switcher, closes it (gone from the state); the panel is in front again
// (after an app closes the window manager may raise the image's own launcher: Q+N then brings ours back); no new core
// dump. -only= runs only the tiles named. The pointer jumps straight to each tile, unless -hand or ON_HAND=1.
// Captures go to OUTDIR (default demo-out/launcher): each app as opened, and each step of the switcher.
import java.awt.image.BufferedImage;
import java.io.File;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import javax.imageio.ImageIO;
import org.sikuli.vnc.VNCScreen;

public class LauncherTest {
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
    static int fails, checks, nas;
    static String host;

    static final String[] PLAIN = {"Terminal", "Readme", "OpticNerve Demo Panel", "GTK4Demo", "Widget Factory"};
    static final String[] GL = {"Developer Desktop", "Gears", "Maze"};

    static void check(String test, String name, boolean ok, String detail) {
        checks++;
        if (!ok) fails++;
        System.out.printf("%-4s %s %s: %s%n", ok ? "PASS" : "FAIL", test, name, detail);
    }

    static void na(String test, String name, String why) {
        nas++;
        System.out.printf("N/A  %s %s: %s%n", test, name, why);
    }

    static void caption(String text) {
        System.out.println();
        System.out.println("==> " + text);
    }

    // One SSH session (a shell on the target, as the login user) for the whole test. rs runs a command as root
    // through sudo, as in host/tools/pi_ssh.sh; the password goes into the session, never to the output.
    static Process ssh;
    static java.io.BufferedReader sshOut;
    static java.io.Writer sshIn;

    static String sh(String commands) throws Exception {
        if (ssh == null) {
            String askpass = new File("host/tools/pi_askpass.sh").getAbsolutePath().replace('\\', '/');
            String user = cfg("ON_LOGIN_USER", "qnxuser");
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
            String pw = cfg("ON_LOGIN_PASSWORD", "").replace("'", "'\\''");
            sshIn.write("rs() { echo '" + pw + "' | sudo -S -p '' \"$@\"; }\n");
        }
        sshIn.write(commands + " 2>&1; echo '#end'\n");
        sshIn.flush();
        StringBuilder said = new StringBuilder();
        for (String l; (l = sshOut.readLine()) != null && !l.equals("#end"); ) said.append(l).append('\n');
        return said.toString();
    }

    // The panel's tiles and where they are, from its log ("tile NAME at X,Y size WxH"; the last start's win).
    static Map<String, int[]> tiles() throws Exception {
        Map<String, int[]> where = new LinkedHashMap<>();
        java.util.regex.Pattern p = java.util.regex.Pattern.compile("^tile (.+) at (\\d+),(\\d+) size (\\d+)x(\\d+)$");
        for (String l : sh("rs grep '^tile ' /data/opticnerve/var/launch.log").split("\n")) {
            java.util.regex.Matcher m = p.matcher(l.trim());
            if (m.matches())
                where.put(m.group(1), new int[] {Integer.parseInt(m.group(2)), Integer.parseInt(m.group(3)),
                        Integer.parseInt(m.group(4)), Integer.parseInt(m.group(5))});
        }
        return where;
    }

    // The apps the panel started that still run (its state file: "GROUP NAME" a line), by name with their groups.
    static Map<String, List<String>> running() throws Exception {
        Map<String, List<String>> r = new LinkedHashMap<>();
        for (String l : sh("cat /dev/shmem/opticnerve-launch.state").split("\n")) {
            String[] g = l.trim().split(" ", 2);
            if (g.length == 2 && g[0].matches("\\d+")) r.computeIfAbsent(g[1], k -> new ArrayList<>()).add(g[0]);
        }
        return r;
    }

    static List<String> dumps() throws Exception {
        List<String> d = new ArrayList<>(Arrays.asList(sh("rs ls /data/var/dumper").trim().split("\n")));
        d.removeIf(String::isEmpty);
        return d;
    }

    static BufferedImage frame() throws Exception {
        return s.capture().getImage();
    }

    static void save(BufferedImage img, String name) throws Exception {
        ImageIO.write(img, "png", new File(out, name + ".png"));
    }

    // Our panels' title bar at (1500, 30), and the demo panel's count box at (165, 285): the launcher panel has the
    // title bar and not the count box (agent/demo/launch_panel.c, demo_panel.c).
    static final int TITLE_BAR = 0x0a3d62, COUNT_BOX = 0x0f1720;

    static boolean launcherInFront(BufferedImage img) {
        return img.getWidth() == 1920 && img.getHeight() == 1080 && (img.getRGB(1500, 30) & 0xffffff) == TITLE_BAR
                && (img.getRGB(165, 285) & 0xffffff) != COUNT_BOX;
    }

    // The mean colour of a box of img, from every 4th pixel each way.
    static double[] mean(BufferedImage img, int x0, int y0, int x1, int y1) {
        double[] m = new double[3];
        int n = 0;
        for (int y = y0; y < y1; y += 4)
            for (int x = x0; x < x1; x += 4) {
                int p = img.getRGB(x, y);
                m[0] += (p >> 16) & 0xff;
                m[1] += (p >> 8) & 0xff;
                m[2] += p & 0xff;
                n++;
            }
        for (int k = 0; k < 3; k++) m[k] /= n;
        return m;
    }

    static double dist(double[] a, double[] b) {
        return Math.sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]));
    }

    static String rgb(double[] m) {
        return String.format("(%.0f,%.0f,%.0f)", m[0], m[1], m[2]);
    }

    // The window switcher at 1920x1080: its strip of thumbnails, and the selected one in the middle of it.
    static final int[] STRIP = {300, 475, 1620, 605}, MIDDLE = {800, 475, 1110, 605};

    // Q held, N tapped, Q let go: one Alt+Tab, which brings the next window to the front.
    static void chordTap() throws Exception {
        handKey("q", true);
        Thread.sleep(400);
        handKey("n", true);
        Thread.sleep(100);
        handKey("n", false);
        Thread.sleep(600);
        handKey("q", false);
        Thread.sleep(1500);
    }

    // The chord as a viewer sends it, aimed: Q held 0.4 s, N held (its auto-repeat, sent as a second N down after
    // 0.5 s, opens the switcher), then N let go and pressed again for each next window. Each step's middle thumbnail
    // is saved and its mean colour taken. When a thumbnail comes round again the switcher has gone through all the
    // windows: the one nearest in colour to the app (as captured when it opened) is the app's, if it is clearly the
    // nearest, and X is pressed when the selection comes back to it. Returns what happened, for the report; xSent
    // tells whether X went out.
    static boolean xSent;

    static String chordClose(String name, double[] app) throws Exception {
        xSent = false;
        String tag = name.replace(' ', '_');
        double[] before = mean(frame(), STRIP[0], STRIP[1], STRIP[2], STRIP[3]);
        List<double[]> seen = new ArrayList<>();
        StringBuilder log = new StringBuilder();
        int period = 0, pick = -1;
        boolean nDown = false, qDown = false;
        try {
            handKey("q", true);
            qDown = true;
            Thread.sleep(400);
            for (int step = 1; step <= 10; step++) {
                handKey("n", true);
                nDown = true;
                if (step == 1) {
                    Thread.sleep(500);
                    handKey("n", true); // the repeat of a held key: the chord begins with N still down
                }
                Thread.sleep(600);
                BufferedImage img = frame();
                save(img, tag + "_switcher_" + step);
                boolean open = dist(mean(img, STRIP[0], STRIP[1], STRIP[2], STRIP[3]), before) > 2;
                double[] m = mean(img, MIDDLE[0], MIDDLE[1], MIDDLE[2], MIDDLE[3]);
                seen.add(m);
                log.append(String.format(" %d:%s%s", step, rgb(m), open ? "" : "(no switcher)"));
                if (!open) break;
                if (period == 0 && step > 1 && dist(m, seen.get(0)) < 2) {
                    period = step - 1;
                    // the app's thumbnail: nearest to the app's colour, and clearly nearer than any other window's
                    int best = -1;
                    double d1 = 1e9, d2 = 1e9;
                    for (int i = 0; i < period; i++) {
                        double d = dist(seen.get(i), app);
                        if (d < d1) { d2 = d1; d1 = d; best = i; }
                        else if (d < d2) d2 = d;
                    }
                    log.append(String.format(" [%d windows; nearest step %d at %.0f, next %.0f]", period, best + 1, d1, d2));
                    if (period == 1 || d2 - d1 < 15) break;   // not sure which is the app: no X
                    pick = best;
                }
                if (period > 0 && pick >= 0 && (step - 1) % period == pick && dist(m, seen.get(pick)) < 2) {
                    handKey("x", true);
                    handKey("x", false);
                    Thread.sleep(600);
                    xSent = true;
                    log.append(" X");
                }
                handKey("n", false);
                nDown = false;
                if (xSent) break;
            }
        } finally {
            if (nDown) handKey("n", false);
            if (qDown) handKey("q", false);
            Thread.sleep(1500);
        }
        return log.toString().trim();
    }

    // Brings our launcher panel to the front with Q+N if it is not there (at most 3 times); true if it is.
    static boolean launcherToFront() throws Exception {
        for (int i = 0; i < 3 && !launcherInFront(frame()); i++)
            chordTap();
        return launcherInFront(frame());
    }

    // Ends whatever the panel started that still runs (their process groups), as launch_panel.sh stop -apps does.
    static String endApps() throws Exception {
        StringBuilder ended = new StringBuilder();
        for (Map.Entry<String, List<String>> e : running().entrySet())
            for (String g : e.getValue()) {
                sh("rs sh -c 'kill -s TERM -- -" + g + "'");
                ended.append(ended.length() > 0 ? ", " : "").append(e.getKey());
            }
        if (ended.length() > 0) Thread.sleep(2000);
        return ended.toString();
    }

    // Any error ends the run as a failure: OculiX's VNC threads would otherwise keep the program alive.
    public static void main(String[] a) {
        int errors = 0;
        try {
            run(a);
        } catch (Throwable e) {
            System.out.println("FAIL the test stopped on an error: " + e);
            errors++;
        }
        try {
            teardown();
        } catch (Throwable e) {
            System.out.println("FAIL TD01 (the panel left as it was found): " + e);
            errors++;
        }
        int code = fails + errors;
        try {
            if (ssh != null) ssh.destroy();
            if (s != null) s.stop();
        } catch (Throwable ignored) {
        }
        System.out.println();
        System.out.printf("%d checks, %d failed, %d N/A%n", checks, code, nas);
        System.out.println(code == 0 ? "ALL PASSED" : code + " FAILED");
        System.exit(code);
    }

    static List<String> dumpsBefore;
    static boolean connected;

    static void run(String[] a) throws Exception {
        host = target(a[0]);
        List<String> args = Arrays.asList(a);
        boolean gl = args.contains("-gl");
        glides = args.contains("-hand") || "1".equals(cfg("ON_HAND", ""));
        List<String> only = null;
        for (String x : a) if (x.startsWith("-only=")) only = Arrays.asList(x.substring(6).split(","));
        out = new File(a.length > 2 && !a[2].startsWith("-") ? a[2] : "demo-out/launcher").getAbsoluteFile();
        out.mkdirs();
        Object p = Class.forName("com.tigervnc.rfb.SecurityClient").getField("secTypes").get(null);
        p.getClass().getMethod("setParam", String.class).invoke(p, "VncAuth,None");

        caption("Setup: connecting to the OpticNerve agent, and finding the launcher panel and its tiles");
        s = VNCScreen.start(host, vncPort(), vncPassword(a[1]), 10, 1000);
        connected = s != null && s.capture() != null;
        check("SU01", "(connect to the agent)", connected, host + ":" + vncPort() + (connected ? "" : ", no frame"));
        if (!connected) return;
        dumpsBefore = dumps();
        Map<String, int[]> where = tiles();
        BufferedImage start = frame();
        save(start, "00_launcher");
        boolean front = launcherInFront(start);
        List<String> missing = new ArrayList<>(Arrays.asList(PLAIN));
        missing.addAll(Arrays.asList(GL));
        missing.removeAll(where.keySet());
        check("SU02", "(the launcher panel in front with its tiles)", front && missing.isEmpty(),
                (front ? "in front" : "NOT in front") + ", " + where.size() + " tiles" + (missing.isEmpty() ? "" : ", missing " + missing));
        Map<String, List<String>> was = running();
        check("SU03", "(no app of the panel running)", was.isEmpty(), was.isEmpty() ? "none" : "running: " + was.keySet());
        if (!front || !missing.isEmpty() || !was.isEmpty()) {
            System.out.println("     setup failed: the tile tests are skipped");
            return;
        }
        double[] launcher = mean(start, 0, 0, 1920, 1080);

        boolean gpu = !sh("test -e /dev/dri/card0 && echo gpu").trim().isEmpty();
        for (String[] set : new String[][] {PLAIN, GL}) {
            boolean isGl = set == GL;
            String id = isGl ? "LP14 (an OpenGL tile opens its app, the key chord closes it)"
                             : "LP13 (a tile opens its app, the key chord closes it)";
            for (String name : set) {
                if (only != null && !only.contains(name)) continue;
                if (isGl && !gl) { na(id, name, "not asked for (-gl)"); continue; }
                if (isGl && !gpu) { na(id, name, "no GPU on this target (no /dev/dri/card0: the virtual display)"); continue; }
                tile(id, name, where.get(name), launcher, isGl);
            }
        }
    }

    // One tile: open, close with the chord, the panel in front again, no new core dump.
    static void tile(String id, String name, int[] t, double[] launcher, boolean isGl) throws Exception {
        String tag = name.replace(' ', '_');
        caption(name + ": clicking its tile");
        List<String> d0 = dumps();
        handClick(t[0] + t[2] / 2 + (int) ((hand.nextDouble() - 0.5) * t[2] / 4),
                  t[1] + t[3] / 2 + (int) ((hand.nextDouble() - 0.5) * t[3] / 4));
        int secs = name.equals("Developer Desktop") ? 30 : 10;
        boolean listed = false, away = false;
        long t0 = System.nanoTime();
        for (long end = t0 + secs * 1_000_000_000L; System.nanoTime() < end && !(listed && away); Thread.sleep(250)) {
            listed = running().containsKey(name);
            away = !launcherInFront(frame());
        }
        double took = (System.nanoTime() - t0) / 1e9;
        Thread.sleep(isGl ? 4000 : 2000);   // the app draws its first frames
        BufferedImage app = frame();
        save(app, tag + "_open");
        check(id, name + ": opens", listed && away, String.format("%s, %s (%.1f s)",
                listed ? "the panel lists it" : "NOT listed by the panel",
                away ? "the panel left the front" : "the panel is STILL in front", took));
        if (!listed) {
            System.out.println("     not started: closing nothing");
            launcherToFront();
            return;
        }

        caption(name + ": closing it with the key chord (hold Q, then N; X on its thumbnail)");
        double[] appMean = mean(app, 0, 0, 1920, 1080);
        String how = chordClose(name, appMean);
        boolean gone = false;
        for (long end = System.nanoTime() + 10_000_000_000L; System.nanoTime() < end && !gone; Thread.sleep(500))
            gone = !running().containsKey(name);
        check(id, name + ": the key chord closes it", xSent && gone,
                (xSent ? "X on its thumbnail" : "X NOT sent") + ", " + (gone ? "gone" : "STILL running")
                        + "; app " + rgb(appMean) + ", launcher " + rgb(launcher) + "; steps" + " " + how);
        if (!gone) System.out.println("     ended over SSH: " + endApps());

        boolean back = launcherToFront();
        if (!back) save(frame(), tag + "_not_back");
        Thread.sleep(3000);   // past the panel's guard against a second start within 3 s
        List<String> d1 = dumps();
        d1.removeAll(d0);
        check(id, name + ": the panel in front again, no new core dump", back && d1.isEmpty(),
                (back ? "in front" : "NOT in front") + ", " + (d1.isEmpty() ? "no new core dump" : "new core dumps " + d1));
    }

    // TD01: whatever is left of the panel's apps is ended, the panel brought to the front; no new core dump.
    static void teardown() throws Exception {
        if (!connected) return;
        caption("Teardown: the launcher panel in front, nothing of it running");
        String ended = endApps();
        boolean back = launcherToFront();
        List<String> d = dumps();
        if (dumpsBefore != null) d.removeAll(dumpsBefore);
        check("TD01", "(the panel left as it was found)", ended.isEmpty() && back && d.isEmpty(),
                (ended.isEmpty() ? "nothing left running" : "ENDED over SSH: " + ended) + ", "
                        + (back ? "panel in front" : "panel NOT in front") + ", "
                        + (d.isEmpty() ? "no new core dump" : "new core dumps " + d));
    }

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
}
