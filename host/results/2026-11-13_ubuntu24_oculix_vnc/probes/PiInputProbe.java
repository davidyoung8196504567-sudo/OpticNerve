// OculiX input probe for the QNX Pi's st terminal. SENDS key and pointer events: run only in an agreed slot.
//   java -cp tigervnc-java-oculix-2.0.4.jar:oculixapi-4.0.0-linux.jar PiInputProbe.java HOST PASSWORD PORT TAP_LOG OUTDIR PHASE
// PORT is rfb_tap.py in front of the Pi's vncserv; TAP_LOG is its log (what OculiX sent). After each step a
// capture is saved to OUTDIR. Phases, run in this order, each on its own connection so the captures can be
// checked in between:
//   start    type "cat -v" + Return in the shell, then one line; cat -v echoes later keys and runs nothing
//   keys     typing, special keys and modifier combinations, one labelled line each
//   launcher pointer functions on the welcome launcher, ending with a click on the Terminal tile
//   type     type the text in $ON_TYPE and Return (login name, passwords, single commands)
//   start, keys  as above, inside st
//   ctrlc    Ctrl+C to leave cat, then a lowercase echo to check Caps Lock
//   ctrld    Ctrl+D (end of input for cat)
//   look     only a capture
import java.io.File;
import java.io.RandomAccessFile;
import java.util.ArrayList;
import java.util.List;
import javax.imageio.ImageIO;
import org.sikuli.script.Key;
import org.sikuli.script.KeyModifier;
import org.sikuli.script.Location;
import org.sikuli.script.Mouse;
import org.sikuli.vnc.VNCScreen;

public class PiInputProbe {
    static RandomAccessFile tap;
    static long tapPos;
    static VNCScreen s;
    static File out;
    static String phase;
    static int n;

    interface Step {
        Object run() throws Throwable;
    }

    // Summarise the RFB input messages the client sent since the last call.
    static String sent() throws Exception {
        byte[] b = new byte[(int) (tap.length() - tapPos)];
        tap.seek(tapPos);
        tap.readFully(b);
        tapPos += b.length;
        List<String> o = new ArrayList<>();
        int moves = 0;
        String first = null, last = null, mask = null;
        for (String line : new String(b).split("\n")) {
            String[] w = line.split(" ");
            if (w[0].equals("PointerEvent")) {
                String m = w[1].substring(5);
                if (m.equals(mask)) {
                    if (moves++ == 0) first = w[2];
                    last = w[2];
                    continue;
                }
                if (moves > 0) o.add("x" + moves + " to " + last);
                moves = 0;
                o.add("mask " + m + "@" + w[2]);
                mask = m;
            } else if (w[0].equals("KeyEvent")) {
                if (moves > 0) o.add("x" + moves + " to " + last);
                moves = 0;
                mask = null;
                o.add((w[1].equals("down") ? "+" : "-") + w[2]);
            } else if (w[0].equals("ClientCutText")) {
                o.add(line);
            }
        }
        if (moves > 0) o.add("x" + moves + " to " + last);
        return o.isEmpty() ? "NOTHING" : String.join(" ", o);
    }

    static void step(String name, Step st) throws Exception {
        String result;
        try {
            result = "returned " + st.run();
        } catch (Throwable e) {
            result = "THREW " + e;
        }
        Thread.sleep(500);
        String file = String.format("%s_%02d.png", phase, ++n);
        ImageIO.write(s.capture().getImage(), "png", new File(out, file));
        System.out.printf("%s: %s%n    OculiX sent: %s%n    capture: %s%n", name, result, sent(), file);
    }

    // Type a label, the keys under test, then Return, so cat -v prints the line back.
    static void line(String label, Step keys) throws Exception {
        step(label, () -> { s.type(label + ":"); Object r = keys.run(); s.type("\r"); return r; });
    }

    public static void main(String[] a) throws Exception {
        tap = new RandomAccessFile(a[3], "r");
        out = new File(a[4]);
        out.mkdirs();
        phase = a[5];
        System.out.println("os " + System.getProperty("os.name") + ", java " + System.getProperty("java.vendor") + " "
                + System.getProperty("java.runtime.version") + ", phase " + phase);
        // ON-VNC-1: limit the security types before the first start.
        try {
            Object p = Class.forName("com.tigervnc.rfb.SecurityClient").getField("secTypes").get(null);
            p.getClass().getMethod("setParam", String.class).invoke(p, "VncAuth,None");
            System.out.println("secTypes: VncAuth,None");
        } catch (Throwable e) {
            System.out.println("secTypes not set: " + e);
        }
        s = VNCScreen.start(a[0], Integer.parseInt(a[2]), a[1], 10, 1000);
        System.out.println("started, bounds " + s.getBounds() + ", image type " + s.capture().getImage().getType());
        Thread.sleep(500);
        tapPos = tap.length();
        Location p1 = s.newLocation(300, 300), p2 = s.newLocation(900, 600);

        if (phase.equals("start")) {
            step("before", () -> "nothing sent");
            step("type(\"cat -v\\r\")", () -> s.type("cat -v\r"));
            step("type(\"abc xyz 0123456789\\r\")", () -> s.type("abc xyz 0123456789\r"));
        } else if (phase.equals("keys")) {
            step("type(\"Hello World QNX\\r\")", () -> s.type("Hello World QNX\r"));
            step("type(\"!@#$%^&*()_+\\r\")", () -> s.type("!@#$%^&*()_+\r"));
            step("type(\"-=[]\\\\;',./`\\r\")", () -> s.type("-=[]\\;',./`\r"));
            step("type(\"{}|:\\\"<>?~\\r\")", () -> s.type("{}|:\"<>?~\r"));
            step("type(\"keyenter:\"), type(Key.ENTER)", () -> { s.type("keyenter:"); return s.type(Key.ENTER); });
            step("type(\"after keyenter\\r\")", () -> s.type("after keyenter\r"));
            line("tab", () -> s.type(Key.TAB + "x"));
            line("bs", () -> s.type("abcd" + Key.BACKSPACE + Key.BACKSPACE));
            line("arrows", () -> s.type(Key.UP + Key.DOWN + Key.LEFT + Key.RIGHT));
            line("nav", () -> s.type(Key.HOME + Key.END + Key.PAGE_UP + Key.INSERT));
            line("del", () -> s.type(Key.DELETE));
            line("f5", () -> s.type(Key.F5));
            line("esc", () -> s.type(Key.ESC));
            line("ctrl-a", () -> s.type("a", KeyModifier.CTRL));
            line("ctrl-shift-a", () -> s.type("a", KeyModifier.CTRL | KeyModifier.SHIFT));
            step("type(\"caps check abc\\r\")", () -> s.type("caps check abc\r"));
            line("alt-x", () -> s.type("x", KeyModifier.ALT));
            line("meta-d", () -> s.type("d", KeyModifier.META));
            line("ctrl-alt-t", () -> s.type("t", KeyModifier.CTRL | KeyModifier.ALT));
            line("shift held", () -> { s.keyDown(Key.SHIFT); s.type("abc1"); s.keyUp(Key.SHIFT); return "ok"; });
            line("ctrl held", () -> { s.keyDown(Key.CTRL); s.type("b"); s.keyUp(Key.CTRL); return "ok"; });
            line("e-acute", () -> s.type("é"));
            line("paste", () -> s.paste("pasted text"));
            step("write(\"write:ab#ENTER.\")", () -> s.write("write:ab#ENTER."));
            step("type(\"keys done\\r\")", () -> s.type("keys done\r"));
        } else if (phase.equals("launcher")) {
            // Pointer functions on the welcome launcher. No double-click on a tile and no wheel (N/A on this image).
            Location gears = s.newLocation(958, 768), terminal = s.newLocation(1107, 460), empty = s.newLocation(300, 600);
            step("before", () -> "nothing sent");
            step("hover(gears tile 958,768)", () -> s.hover(gears));
            step("hover(terminal tile 1107,460)", () -> s.hover(terminal));
            step("mouseMove(empty 300,600)", () -> s.mouseMove(empty));
            step("click(empty 300,600)", () -> s.click(empty));
            step("rightClick(empty 300,600)", () -> s.rightClick(empty));
            step("dragDrop(empty 300,600 -> 500,650)", () -> s.dragDrop(empty, s.newLocation(500, 650)));
            step("mouseDown(Mouse.LEFT), mouseMove(300,600), mouseUp(Mouse.LEFT)", () -> { s.mouseDown(Mouse.LEFT); s.mouseMove(empty); s.mouseUp(Mouse.LEFT); return "ok"; });
            step("mouseDown LEFT, MIDDLE, RIGHT then mouseUp(LEFT|MIDDLE|RIGHT)", () -> {
                s.mouseDown(Mouse.LEFT); s.mouseDown(Mouse.MIDDLE); s.mouseDown(Mouse.RIGHT);
                Thread.sleep(200);
                s.mouseUp(Mouse.LEFT | Mouse.MIDDLE | Mouse.RIGHT);
                return "ok";
            });
            step("click(terminal tile 1107,460)", () -> { Object r = s.click(terminal); Thread.sleep(3000); return r; });
        } else if (phase.equals("type")) {
            // Type the text given in the environment variable ON_TYPE, then Return (for the login name and passwords).
            step("type(text from ON_TYPE + \"\\r\")", () -> { Object r = s.type(System.getenv("ON_TYPE") + "\r"); Thread.sleep(2500); return r; });
        } else if (phase.equals("ctrlc")) {
            step("type(\"c\", KeyModifier.CTRL)", () -> s.type("c", KeyModifier.CTRL));
            step("type(\"echo caps check abc\\r\")", () -> s.type("echo caps check abc\r"));
        } else if (phase.equals("ctrld")) {
            step("type(\"d\", KeyModifier.CTRL)", () -> s.type("d", KeyModifier.CTRL));
        } else if (phase.equals("look")) {
            step("look", () -> "nothing sent");
        }
        s.stop();
        System.out.println("stopped");
        System.exit(0);
    }
}
