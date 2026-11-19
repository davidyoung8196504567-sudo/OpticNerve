// The OpticNerve demo test: OculiX drives the demo panel (agent/demo/demo_panel.c) on a QNX 8.0 target through
// the OpticNerve agent, and checks what the panel shows. Each step prints PASS or FAIL; the exit status is the
// number of failures.
//
//   java -cp "tigervnc-java-oculix-2.0.4.jar;oculixapi-4.0.0-windows.jar" host/demo/DemoTest.java HOST PASSWORD [OUTDIR] [-fail] [-slow] [-cols=N] [-brief] [-repeat=N]
//
// Needs the agent running on HOST (port 5901) and demo_panel showing. -fail adds one check that expects the wrong
// count, to show what a caught failure looks like. Between steps the test asks the agent on the target, over SSH,
// to wait until the screen has settled (`opticnerve wait -quiet`), when ON_LOGIN_PASSWORD is set; otherwise it
// waits a fixed time. Configuration ON-VNC-1: the tigervnc-java-oculix jar first, security types VncAuth,None.
//
// Before each step the test prints a caption line ("==> ..."), for the video's captions. -slow paces the run for
// recording: a pause after each caption, and slower pointer moves (OculiX Settings.MoveMouseDelay), so the demo
// panel's pointer trail can be followed. -brief prints only the PASS and FAIL lines (normal test speed, no captions).
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

    static void caption(String text) throws Exception {
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
    // draws each response in one frame; 300 ms otherwise, for the pointer marker's animation).
    static long seen = -1;

    static void settle() throws Exception {
        if (System.getenv("ON_LOGIN_PASSWORD") == null) {
            Thread.sleep(700);
            return;
        }
        long t0 = System.nanoTime();
        String said = agent("wait -since=" + seen + " -quiet=" + (brief ? 100 : 300) + " -timeout=3");
        if (!brief) System.out.printf("     opticnerve wait: %s (%.1f s)%n", said, (System.nanoTime() - t0) / 1e9);
    }

    // Before the first action: the change count now.
    static void startCount() throws Exception {
        if (System.getenv("ON_LOGIN_PASSWORD") != null) agent("wait -since=0 -timeout=1");
    }

    // Runs `opticnerve ARGS` on the target and returns its output; notes the change count it reports.
    static String agent(String args) throws Exception {
        if (ssh == null) {
            String askpass = new File("host/tools/pi_askpass.sh").getAbsolutePath().replace('\\', '/');
            // Git's ssh through bash (askpass is a shell script); no quotes inside the bash command, since Java on
            // Windows does not pass embedded quotes through intact
            String user = System.getenv().getOrDefault("ON_LOGIN_USER", "qnxuser");
            ProcessBuilder pb = new ProcessBuilder("bash", "-c", "exec ssh $@", "ssh", "-q", "-o", "PubkeyAuthentication=no",
                    "-o", "ConnectTimeout=10", "-o", "StrictHostKeyChecking=accept-new", user + "@" + host, "sh -s")
                    .redirectErrorStream(true);
            pb.environment().put("SSH_ASKPASS", askpass);
            pb.environment().put("SSH_ASKPASS_REQUIRE", "force");
            pb.environment().put("DISPLAY", "x");
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

    // OCR of a region with OculiX. The panel's letters are a blocky pixel font; Tesseract reads them at the sizes the
    // panel uses, and not when scaled (tried on 2026-11-16: worse).
    static String read(int x, int y, int w, int h) {
        Region r = s.newRegion(x, y, w, h);
        return r.text().replaceAll("\\s+", " ").trim();
    }

    // The count is checked by image: the count box must match a reference picture of that number (host/demo/ref/),
    // recorded once with -record and checked by eye. OCR guessed wrongly at the panel's single blocky digits, and the
    // agent's frames are exact, so a reference picture is the reliable check. -record saves instead of checking.
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
        // For up to 1 s: at full speed the check can come before the panel's redraw (a take of 2026-11-17 failed so).
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
        host = a[0];
        boolean showFailure = java.util.Arrays.asList(a).contains("-fail");
        record = java.util.Arrays.asList(a).contains("-record");
        slow = java.util.Arrays.asList(a).contains("-slow");
        brief = java.util.Arrays.asList(a).contains("-brief");
        int repeat = 1;
        for (String x : a) if (x.startsWith("-repeat=")) repeat = Integer.parseInt(x.substring(8));
        for (String x : a) if (x.startsWith("-cols=")) cols = Integer.parseInt(x.substring(6));
        out = new File(a.length > 2 && !a[2].startsWith("-") ? a[2] : "demo-out").getAbsoluteFile();
        out.mkdirs();
        Object p = Class.forName("com.tigervnc.rfb.SecurityClient").getField("secTypes").get(null);
        p.getClass().getMethod("setParam", String.class).invoke(p, "VncAuth,None");
        if (slow) org.sikuli.basics.Settings.MoveMouseDelay = 1.0f;
        if (brief) org.sikuli.basics.Settings.MoveMouseDelay = 0; // the pointer jumps: only the search and the panel's response take time
        caption("Connecting to the QNX 8.0 target through the OpticNerve agent (VNC, port 5901)");
        s = VNCScreen.start(a[0], 5901, a[1], 10, 1000);
        boolean framed = s != null && s.capture() != null; // a frame arrived, not just an object returned
        check("connect to the OpticNerve agent", framed, a[0] + ":5901" + (framed ? "" : ", no frame"));
        if (!framed) return;
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

    // OculiX's click, holding the button 50 ms as a hand does (ClickDelay; OculiX resets it after each click), as in
    // ObstacleCourse.java: with the default 0 the press and release go out together.
    static void click(Pattern target) throws Exception {
        org.sikuli.basics.Settings.ClickDelay = 0.05;
        s.click(target);
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
        s.type("\r");  // Return clears the text line (Enter as "\r", ON-VNC-1)
        settle();
        s.type("hello pi 5");
        settle();
        String typed = read(400, 505, 1360, 80);
        for (long end = System.nanoTime() + 1_000_000_000L; !typed.equals("HELLO PI 5") && System.nanoTime() < end; )
            typed = read(400, 505, 1360, 80); // up to 1 s, as for the count
        check("typed text appears on the panel", typed.equals("HELLO PI 5"), "OCR read \"" + typed + "\"");
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
