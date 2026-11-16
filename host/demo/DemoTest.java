// The OpticNerve demo test: OculiX drives the demo panel (agent/demo/demo_panel.c) on a QNX 8.0 target through
// the OpticNerve agent, and checks what the panel shows. Each step prints PASS or FAIL; the exit status is the
// number of failures.
//
//   java -cp "tigervnc-java-oculix-2.0.4.jar;oculixapi-4.0.0-windows.jar" host/demo/DemoTest.java HOST PASSWORD [OUTDIR] [-fail] [-slow] [-cols=N]
//
// Needs the agent running on HOST (port 5901) and demo_panel showing. -fail adds one check that expects the wrong
// count, to show what a caught failure looks like. Between steps the test asks the agent on the target, over SSH,
// to wait until the screen has settled (`opticnerve wait -quiet`), when ON_LOGIN_PASSWORD is set; otherwise it
// waits a fixed time. Configuration ON-VNC-1: the tigervnc-java-oculix jar first, security types VncAuth,None.
//
// Before each step the test prints a caption line ("==> ..."), for the video's captions. -slow paces the run for
// recording: a pause after each caption, and slower pointer moves (OculiX Settings.MoveMouseDelay), so the demo
// panel's pointer trail can be followed.
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

    static boolean slow;
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
        System.out.println();
        say("==> ", text);
        if (slow) Thread.sleep(1500);
    }

    static void check(String name, boolean ok, String detail) {
        if (!ok) fails++;
        if (cols > 0) {
            say(ok ? "PASS " : "FAIL ", name);
            say("     ", detail);
        } else {
            System.out.printf("%-4s %-44s %s%n", ok ? "PASS" : "FAIL", name, detail);
        }
    }

    // The agent on the target says when the screen has settled; without SSH, a fixed pause.
    static void settle() throws Exception {
        if (System.getenv("ON_LOGIN_PASSWORD") == null) {
            Thread.sleep(700);
            return;
        }
        long t0 = System.nanoTime();
        Process p = new ProcessBuilder("bash", "host/tools/pi_ssh.sh",
                "/data/opticnerve/bin/opticnerve wait -quiet=300 -timeout=3").redirectErrorStream(true).start();
        String said = new String(p.getInputStream().readAllBytes()).trim();
        p.waitFor();
        System.out.printf("     opticnerve wait: %s (%.1f s)%n", said, (System.nanoTime() - t0) / 1e9);
    }

    static void shot(String name) throws Exception {
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
        BufferedImage want = ImageIO.read(ref), got = s.capture(160, 280, 700, 180).getImage();
        int differ = 0;
        for (int y = 0; y < want.getHeight(); y++)
            for (int x = 0; x < want.getWidth(); x++)
                if ((want.getRGB(x, y) & 0xffffff) != (got.getRGB(x, y) & 0xffffff)) differ++;
        return differ == 0 ? "equals " + ref.getName() + " (0 pixels differ)" : null;
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
            if (s != null) s.stop();
        } catch (Throwable ignored) {
        }
        System.out.println();
        System.out.println(code == 0 ? "ALL PASSED" : code + " FAILED");
        System.exit(code);
    }

    static void run(String[] a) throws Exception {
        boolean showFailure = java.util.Arrays.asList(a).contains("-fail");
        record = java.util.Arrays.asList(a).contains("-record");
        slow = java.util.Arrays.asList(a).contains("-slow");
        for (String x : a) if (x.startsWith("-cols=")) cols = Integer.parseInt(x.substring(6));
        out = new File(a.length > 2 && !a[2].startsWith("-") ? a[2] : "demo-out").getAbsoluteFile();
        out.mkdirs();
        Object p = Class.forName("com.tigervnc.rfb.SecurityClient").getField("secTypes").get(null);
        p.getClass().getMethod("setParam", String.class).invoke(p, "VncAuth,None");
        if (slow) org.sikuli.basics.Settings.MoveMouseDelay = 1.0f;
        caption("Connecting to the QNX 8.0 target through the OpticNerve agent (VNC, port 5901)");
        s = VNCScreen.start(a[0], 5901, a[1], 10, 1000);
        boolean framed = s != null && s.capture() != null; // a frame arrived, not just an object returned
        check("connect to the OpticNerve agent", framed, a[0] + ":5901" + (framed ? "" : ", no frame"));
        if (!framed) return;
        shot("start");

        caption("Finding the RESET button by image, and clicking it");
        Pattern plus = cut(160, 700, 420, 200, "plus"), reset = cut(750, 700, 420, 200, "reset"),
                lampButton = cut(1340, 700, 420, 200, "lamp");
        Match m = s.exists(reset, 3);
        check("find the RESET button by image", m != null, m == null ? "not found" : String.format("score %.4f", m.getScore()));
        s.click(reset);
        settle();
        caption("Checking the count shows 0, pixel by pixel");
        String r0 = countIs(0);
        check("after RESET the count shows 0", r0 != null, r0 == null ? "no match with count_0.png" : r0);

        caption("Clicking +1 three times; the agent on the target says when the screen has settled");
        for (int i = 1; i <= 3; i++) {
            s.click(plus);
            settle();
        }
        caption("Checking the count shows 3");
        String r3 = countIs(3);
        check("after three clicks on +1 the count shows 3", r3 != null, r3 == null ? "no match with count_3.png" : r3);
        shot("count3");

        caption("Clicking LAMP and checking the lamp changes colour");
        String before = lamp();
        s.click(lampButton);
        settle();
        String after = lamp();
        check("LAMP switches the lamp's colour", !before.equals(after), before + " -> " + after);
        shot("lamp");

        caption("Typing on the target's keyboard input, and reading the text back with OCR");
        s.type("\r");  // Return clears the text line (Enter as "\r", ON-VNC-1)
        settle();
        s.type("hello pi 5");
        settle();
        String typed = read(400, 505, 1360, 80);
        check("typed text appears on the panel", typed.equals("HELLO PI 5"), "OCR read \"" + typed + "\"");
        shot("typed");

        if (showFailure && !record) {
            caption("A check that expects the wrong count, to show a failure being caught");
            String wrong = countIs(0);
            check("(deliberate) the count still shows 0", wrong != null, wrong == null ? "no match with count_0.png" : wrong);
        }
    }
}
