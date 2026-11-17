// A test of the QNX 8.0 quick-start image's Developer Desktop (Weston with Xfce), driven by OculiX through the
// OpticNerve agent: start the desktop (over SSH, as the launcher starts it: the launcher does not act on injected
// input), open the web browser from the Applications menu, go to the OpticNerve repository on GitHub, show its banner
// full screen, close the desktop. Each step prints PASS or FAIL; the exit status is the number of failures.
//
//   java -cp "tigervnc-java-oculix-2.0.4.jar;oculixapi-4.0.0-windows.jar" host/demo/DesktopTest.java HOST VNCPASSWORD [-hold=S] [-cols=N]
//
// Needs the agent running on HOST (port 5901), ON_LOGIN_PASSWORD set (for SSH) and the target online. -hold=S keeps
// the banner on the screen for S seconds before the desktop is closed (for the video).
import org.sikuli.script.Key;
import org.sikuli.vnc.VNCScreen;

public class DesktopTest {
    static VNCScreen s;
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

    static void caption(String text) {
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
        Process p = new ProcessBuilder("bash", "host/tools/pi_ssh.sh", commands).redirectErrorStream(true).start();
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

    // One character at a time: typed at full speed (2026-11-17), the address bar got some characters out of order
    // and lost others (to be investigated with the Shift case above).
    static void typeSlowly(String text) throws Exception {
        for (char c : text.toCharArray()) {
            s.type(String.valueOf(c));
            Thread.sleep(40);
        }
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
        org.sikuli.basics.Settings.MoveMouseDelay = 0.5f;

        caption("Connecting to the QNX 8.0 target through the OpticNerve agent");
        s = VNCScreen.start(a[0], 5901, a[1], 10, 1000);
        boolean framed = s != null && s.capture() != null;
        check("connect to the OpticNerve agent", framed, a[0] + ":5901");
        if (!framed) return;

        caption("Starting the Developer Desktop on the target, as the launcher does (over SSH)");
        String started = ssh("cd /; rs slay -f weston >/dev/null 2>&1; sleep 1; rs env -i " + LAUNCHER_ENV
                + " sh -c 'nohup " + START + " >/dev/null 2>&1 </dev/null &'; sleep 1; echo started");
        String panel = started.contains("started") ? waitFor("Applications", 0, 0, 300, 28, 20) : "";
        check("the desktop shows its Applications menu", has(panel, "Applications"), "OCR read \"" + shortened(panel) + "\"");
        if (!has(panel, "Applications")) return;
        settle(1000, 10);

        caption("Opening the web browser from the Applications menu");
        s.click(s.newLocation(MENU_X, MENU_Y));
        String menu = waitFor("Web Browser", 0, 28, 340, 320, 5);
        check("the menu lists the web browser", has(menu, "Web Browser"), "OCR read \"" + shortened(menu) + "\"");
        if (!has(menu, "Web Browser")) return;
        java.awt.image.BufferedImage before = s.capture(760, 440, 400, 200).getImage();
        s.click(s.newLocation(BROWSER_X, BROWSER_Y));
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
        s.click(s.newLocation(ADDRESS_X, ADDRESS_Y));
        Thread.sleep(300);
        typeSlowly(URL);
        settle(1000, 10); // the address bar's suggestions come first; Enter typed while they load was lost
        s.type("\r"); // Return; Key.ENTER is a line feed, which the address bar ignores
        // "Public" is the badge beside the repository's name: on the page, not in the address bar's suggestions.
        String page = waitFor("Public", 0, 0, 1920, 1080, 30);
        boolean repo = has(page, "Public") && has(page, "davidyoung8196504567-sudo") && has(page, "OpticNerve");
        check("the browser shows the OpticNerve repository", repo,
                repo ? "OCR read the owner, the project name and Public" : "OCR read \"" + shortened(page) + "\"");
        if (!repo) failShot("repository");
        settle(1500, 20);

        caption("Full screen, and down to the README's banner");
        s.type(Key.F11);
        settle(800, 10);
        s.type(Key.PAGE_DOWN);
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
