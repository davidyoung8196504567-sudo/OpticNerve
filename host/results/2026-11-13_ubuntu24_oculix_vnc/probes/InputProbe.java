// OculiX VNC input probe. SENDS pointer and key events: only for a VNC server nobody else is using.
//   java -cp oculixapi-4.0.0-linux.jar InputProbe.java HOST PASSWORD PORT XEV_LOG TAP_LOG
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
import org.sikuli.vnc.VNCScreen;

public class InputProbe {
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
        step("mouseMove(900,600)", () -> s.mouseMove(p2));
        step("click(300,300)", () -> s.click(p1));
        step("doubleClick(900,600)", () -> s.doubleClick(p2));
        step("rightClick(300,300)", () -> s.rightClick(p1));
        step("mouseDown(LEFT) then mouseUp()", () -> { s.mouseDown(Button.LEFT); Thread.sleep(200); s.mouseUp(); return "ok"; });
        step("mouseDown(MIDDLE) then mouseUp()", () -> { s.mouseDown(Button.MIDDLE); Thread.sleep(200); s.mouseUp(); return "ok"; });
        step("wheel(900,600, WHEEL_DOWN, 3)", () -> s.wheel(p2, Button.WHEEL_DOWN, 3));
        step("wheel(900,600, WHEEL_UP, 2)", () -> s.wheel(p2, Button.WHEEL_UP, 2));
        step("dragDrop(300,300 -> 900,600)", () -> s.dragDrop(p1, p2));
        step("drag(900,600) then dropAt(300,300)", () -> { s.drag(p2); return s.dropAt(p1); });

        step("type(\"abc\")", () -> s.type("abc"));
        step("type(\"Hello World\")", () -> s.type("Hello World"));
        step("type(\"0123456789\")", () -> s.type("0123456789"));
        step("type(\"!@#$%^&*()_+\")", () -> s.type("!@#$%^&*()_+"));
        step("type(\"-=[]\\\\;',./`\")", () -> s.type("-=[]\\;',./`"));
        step("type(\"{}|:\\\"<>?~\")", () -> s.type("{}|:\"<>?~"));
        step("type(\"\\r\")", () -> s.type("\r"));
        step("type(Key.ENTER)", () -> s.type(Key.ENTER));
        step("type(Key.TAB)", () -> s.type(Key.TAB));
        step("type(Key.ESC)", () -> s.type(Key.ESC));
        step("type(Key.BACKSPACE)", () -> s.type(Key.BACKSPACE));
        step("type(Key.DELETE)", () -> s.type(Key.DELETE));
        step("type(Key.SPACE)", () -> s.type(Key.SPACE));
        step("type(UP DOWN LEFT RIGHT)", () -> s.type(Key.UP + Key.DOWN + Key.LEFT + Key.RIGHT));
        step("type(HOME END PAGE_UP INSERT)", () -> s.type(Key.HOME + Key.END + Key.PAGE_UP + Key.INSERT));
        step("type(Key.F5)", () -> s.type(Key.F5));
        step("type(\"a\", KeyModifier.CTRL)", () -> s.type("a", KeyModifier.CTRL));
        step("type(\"a\", KeyModifier.CTRL | KeyModifier.SHIFT)", () -> s.type("a", KeyModifier.CTRL | KeyModifier.SHIFT));
        step("type(\"t\", KeyModifier.CTRL | KeyModifier.ALT)", () -> s.type("t", KeyModifier.CTRL | KeyModifier.ALT));
        step("type(Key.TAB, KeyModifier.ALT)", () -> s.type(Key.TAB, KeyModifier.ALT));
        step("type(Key.DELETE, KeyModifier.ALT)", () -> s.type(Key.DELETE, KeyModifier.ALT));
        step("type(\"d\", KeyModifier.META)", () -> s.type("d", KeyModifier.META));
        step("type(\"a\", Key.CTRL + Key.SHIFT)", () -> s.type("a", Key.CTRL + Key.SHIFT));
        step("keyDown(Key.CTRL), type(\"c\"), keyUp(Key.CTRL)", () -> { s.keyDown(Key.CTRL); s.type("c"); s.keyUp(Key.CTRL); return "ok"; });
        step("keyDown(Key.SHIFT), click(300,300), keyUp()", () -> { s.keyDown(Key.SHIFT); s.click(p1); s.keyUp(); return "ok"; });
        step("write(\"ab#ENTER.\")", () -> s.write("ab#ENTER."));
        step("type(\"\\u00e9\") (e acute)", () -> s.type("\u00e9"));
        step("paste(\"pasted text\")", () -> s.paste("pasted text"));

        s.stop();
        System.out.println("stopped");
        System.exit(0);
    }
}
