// A sanity test for a freshly installed QNX 8.0 quick-start image, driven by OculiX through the OpticNerve agent:
// open a Terminal (started over SSH as the launcher starts it, as root with its environment: the launcher does not
// act on injected input), check
// the login banner, log in, check that the message of the day is shown and that the shell answers, log out, close
// the Terminal. Each step prints PASS or FAIL; the exit status is the number of failures. The text read from the
// screen is printed too.
//
//   java -cp "tigervnc-java-oculix-2.0.4.jar;oculixapi-4.0.0-windows.jar" host/demo/LoginTest.java HOST VNCPASSWORD [-nologin] [-cols=N]
//
// Needs the agent running on HOST (port 5901) and ON_LOGIN_PASSWORD set (for SSH, and for the login). The login
// name is ON_LOGIN_USER (default qnxuser); the password is typed into the login prompt, which does not echo it, and
// is never printed. -nologin types the name and stops at the password prompt.
import java.awt.image.BufferedImage;

import org.sikuli.vnc.VNCScreen;

public class LoginTest {
    static VNCScreen s;
    static int fails, cols;

    // The banner and the message of the day of the quick-start image of 2026-09-02 (package quickstart.rpi5
    // 0.5.0.00050): lines that OCR reads reliably; the logo in block characters is not checked.
    static final String[] BANNER = { "QNX", "8.0.0", "RaspberryPi5", "aarch64le" };
    static final String[] MOTD = {
        "Welcome to QNX 8.0 Non-Commercial!",
        "This QNX 8 target is now ready for your QNX applications.",
        "(You can use ALT-TAB to switch between windows.)",
    };

    // The launcher's Terminal entry (/system/etc/desktop_files/terminal.desktop) runs /usr/bin/st /usr/etc/st_boot.sh
    // as root with the launcher's environment. Started with an SSH login's environment instead, the Terminal's login
    // never gets to the password prompt, with the real keyboard too (2026-11-16); with this environment it does.
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
        say("     ", detail);
    }

    // OCR of a region, whitespace folded.
    static String read(int x, int y, int w, int h) {
        return s.newRegion(x, y, w, h).text().replaceAll("[ \t]+", " ").trim();
    }

    // Whether the screen text contains the line, comparing letters and digits only: OCR varies in punctuation and
    // spacing (the first full run read the MOTD's "(You can use ALT-TAB to switch between windows.)" differently).
    static boolean has(String screen, String line) {
        return fold(screen).contains(fold(line));
    }

    static String fold(String s) {
        return s.toLowerCase().replaceAll("[^a-z0-9]", "");
    }

    // Reads the region until it contains want, for at most secs seconds; returns the last text read.
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

    // Runs commands on the target over SSH (host/tools/pi_ssh.sh, which reads ON_LOGIN_PASSWORD); returns the output.
    static String ssh(String commands) throws Exception {
        Process p = new ProcessBuilder("bash", "host/tools/pi_ssh.sh", commands).redirectErrorStream(true).start();
        String out = new String(p.getInputStream().readAllBytes()).trim();
        p.waitFor();
        return out;
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
            ssh("rs slay -f st >/dev/null 2>&1"); // close the Terminal; the launcher comes back
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
        boolean login = !java.util.Arrays.asList(a).contains("-nologin");
        for (String x : a) if (x.startsWith("-cols=")) cols = Integer.parseInt(x.substring(6));
        String user = System.getenv().getOrDefault("ON_LOGIN_USER", "qnxuser");
        String password = System.getenv("ON_LOGIN_PASSWORD");
        if (password == null) throw new IllegalStateException("ON_LOGIN_PASSWORD is not set");
        Object p = Class.forName("com.tigervnc.rfb.SecurityClient").getField("secTypes").get(null);
        p.getClass().getMethod("setParam", String.class).invoke(p, "VncAuth,None");

        caption("Connecting to the QNX 8.0 target through the OpticNerve agent");
        s = VNCScreen.start(a[0], 5901, a[1], 10, 1000);
        BufferedImage first = s == null ? null : s.capture().getImage();
        check("connect to the OpticNerve agent", first != null, a[0] + ":5901");
        if (first == null) return;

        caption("Opening a Terminal on the target, as the launcher does (over SSH)");
        String started = ssh("cd /; rs env -i " + LAUNCHER_ENV
                + " sh -c 'nohup /usr/bin/st /usr/etc/st_boot.sh >/dev/null 2>&1 </dev/null &'; sleep 1; echo started");
        check("start the Terminal (st)", started.contains("started"), started.isEmpty() ? "no answer over SSH" : "/usr/bin/st /usr/etc/st_boot.sh");

        caption("Checking the login banner");
        String top = waitFor("login:", 0, 0, 1920, 60, 10);
        boolean banner = true;
        for (String b : BANNER) banner &= has(top, b);
        check("the Terminal shows the banner and a login prompt", banner && has(top, "login:"), "OCR read \"" + top.replace('\n', ' ') + "\"");
        if (!has(top, "login:")) return;

        caption("Logging in as " + user + (login ? " (the password comes from the environment and is not shown)" : ""));
        s.type(user + "\r");
        String pw = waitFor("Password:", 0, 0, 1920, 80, 5);
        check("the login asks for a password", has(pw, "Password:"), "OCR read \"" + pw.replace('\n', ' ') + "\"");
        if (!login) return;
        s.type(password + "\r");
        password = null;

        caption("Checking the message of the day");
        String screen = waitFor(MOTD[MOTD.length - 1], 0, 0, 1920, 1080, 10);
        for (String m : MOTD) check("the message of the day shows a line", has(screen, m), "\"" + m + "\"");

        caption("Checking the shell answers");
        s.type("echo shell-ok-$((6*7))\r");
        String answer = waitFor("shell-ok-42", 0, 0, 1920, 1080, 5);
        check("the shell runs a command", has(answer, "shell-ok-42"), "echo shell-ok-$((6*7)) printed shell-ok-42");

        caption("The screen as OculiX read it");
        for (String l : answer.split("\n")) if (!l.isBlank()) say("  | ", l);

        caption("Logging out");
        s.type("exit\r");
        String again = waitFor("login:", 0, 0, 1920, 1080, 5);
        check("logging out returns to a login prompt", has(again, "login:"), has(again, "login:") ? "login: shown" : "no login prompt");
    }
}
