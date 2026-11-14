// OculiX VNC mouseDown probe (Button.* vs Mouse.* constants). SENDS pointer and key events: only for a VNC server nobody else is using.
//   java -cp oculixapi-4.0.0-linux.jar RecoveryProbe.java HOST PASSWORD PORT XEV_LOG TAP_LOG
// The target display shows a full-screen xev window whose output (line-buffered) goes to XEV_LOG on this
// machine. After each step the new xev events are summarised, so each line shows what the X server received.
// PORT is the port of rfb_tap.py in front of the server; TAP_LOG is its log, which shows what OculiX sent.
import java.io.RandomAccessFile;
import java.util.ArrayList;
import java.util.List;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import org.sikuli.script.Button;
import org.sikuli.script.Key;
import org.sikuli.script.KeyModifier;
import org.sikuli.script.Location;
import org.sikuli.script.Mouse;
import org.sikuli.vnc.VNCScreen;

public class RecoveryProbe {
    static RandomAccessFile log, tap;
    static long pos, tapPos;

    interface Step {
        Object run() throws Throwable;
    }

    static final Pattern ROOT = Pattern.compile("root:\\((-?\\d+),(-?\\d+)\\)");
    static final Pattern STATE = Pattern.compile("state (0x[0-9a-f]+)");
    static final Pattern KEYSYM = Pattern.compile("keysym (0x[0-9a-f]+), ([^)]+)\\)");
    static final Pattern BUTTON = Pattern.compile("button (\\d+)");

    static String group(Pattern p, String s, int g) {
        Matcher m = p.matcher(s);
        return m.find() ? m.group(g) : "?";
    }

    // Summarise the xev events written since the last call.
    static String events() throws Exception {
        Thread.sleep(700);
        byte[] b = new byte[(int) (log.length() - pos)];
        log.seek(pos);
        log.readFully(b);
        pos += b.length;
        List<String> out = new ArrayList<>();
        int motions = 0;
        String firstMotion = null, lastMotion = null;
        for (String block : new String(b).split("\n\n")) {
            String type = block.trim().split(" ")[0];
            String at = group(ROOT, block, 1) + "," + group(ROOT, block, 2);
            if (type.equals("MotionNotify")) {
                if (motions++ == 0) firstMotion = at;
                lastMotion = at;
                continue;
            }
            if (motions > 0) {
                out.add("Motion x" + motions + " " + firstMotion + " -> " + lastMotion);
                motions = 0;
            }
            if (type.equals("KeyPress") || type.equals("KeyRelease")) {
                out.add((type.equals("KeyPress") ? "+" : "-") + group(KEYSYM, block, 2) + "(" + group(KEYSYM, block, 1)
                        + ",state " + group(STATE, block, 1) + ")");
            } else if (type.equals("ButtonPress") || type.equals("ButtonRelease")) {
                out.add((type.equals("ButtonPress") ? "+" : "-") + "button" + group(BUTTON, block, 1) + "@" + at
                        + "(state " + group(STATE, block, 1) + ")");
            }
        }
        if (motions > 0) out.add("Motion x" + motions + " " + firstMotion + " -> " + lastMotion);
        return out.isEmpty() ? "NO EVENTS" : String.join(" ", out);
    }

    // Summarise the RFB input messages the client sent since the last call.
    static String sent() throws Exception {
        byte[] b = new byte[(int) (tap.length() - tapPos)];
        tap.seek(tapPos);
        tap.readFully(b);
        tapPos += b.length;
        List<String> out = new ArrayList<>();
        int moves = 0;
        String first = null, last = null, mask = "0";
        for (String line : new String(b).split("\n")) {
            String[] w = line.split(" ");
            if (w[0].equals("PointerEvent")) {
                String m = w[1].substring(5);
                if (m.equals(mask)) {
                    if (moves++ == 0) first = w[2];
                    last = w[2];
                    continue;
                }
                if (moves > 0) out.add("move x" + moves + " " + first + " -> " + last + (mask.equals("0") ? "" : " [mask " + mask + "]"));
                moves = 0;
                out.add("mask " + mask + "->" + m + "@" + w[2]);
                mask = m;
            } else if (w[0].equals("KeyEvent")) {
                if (moves > 0) out.add("move x" + moves + " " + first + " -> " + last + (mask.equals("0") ? "" : " [mask " + mask + "]"));
                moves = 0;
                out.add((w[1].equals("down") ? "+" : "-") + w[2]);
            } else if (w[0].equals("ClientCutText")) {
                out.add(line);
            }
        }
        if (moves > 0) out.add("move x" + moves + " " + first + " -> " + last + (mask.equals("0") ? "" : " [mask " + mask + "]"));
        return out.isEmpty() ? "NOTHING" : String.join(" ", out);
    }

    static void step(String name, Step s) throws Exception {
        long t = System.nanoTime();
        String result;
        try {
            result = "returned " + s.run();
        } catch (Throwable e) {
            StackTraceElement[] st = e.getStackTrace();
            result = "THREW " + e + " at " + (st.length > 0 ? st[0] : "?");
        }
        double secs = (System.nanoTime() - t) / 1e9;
        String got = events();
        System.out.printf("%s: %s in %.2f s%n    OculiX sent:  %s%n    X server got: %s%n", name, result, secs, sent(), got);
    }

    public static void main(String[] a) throws Exception {
        log = new RandomAccessFile(a[3], "r");
        tap = new RandomAccessFile(a[4], "r");
        System.out.println("os " + System.getProperty("os.name") + ", java " + System.getProperty("java.version"));
        VNCScreen s = VNCScreen.start(a[0], Integer.parseInt(a[2]), a[1], 10, 1000);
        System.out.println("started, bounds " + s.getBounds() + ", image type " + s.capture().getImage().getType());
        Thread.sleep(1000);
        pos = log.length();
        tapPos = tap.length();
        Location p1 = s.newLocation(300, 300), p2 = s.newLocation(900, 600);

        step("hover(300,300)", () -> s.hover(p1));
        step("mouseDown(Mouse.LEFT)", () -> { s.mouseDown(Mouse.LEFT); return "ok"; });
        step("mouseDown(Mouse.MIDDLE)", () -> { s.mouseDown(Mouse.MIDDLE); return "ok"; });
        step("mouseDown(Mouse.RIGHT)", () -> { s.mouseDown(Mouse.RIGHT); return "ok"; });
        step("mouseUp() bare", () -> { s.mouseUp(); return "ok"; });
        step("mouseUp(Mouse.LEFT | Mouse.MIDDLE | Mouse.RIGHT)", () -> { s.mouseUp(Mouse.LEFT | Mouse.MIDDLE | Mouse.RIGHT); return "ok"; });
        step("click(900,600) afterwards", () -> s.click(p2));
        s.stop();
        System.out.println("stopped");
        System.exit(0);
    }
}
