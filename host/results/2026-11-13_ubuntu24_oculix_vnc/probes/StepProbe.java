// Scripted OculiX input over VNC. SENDS key and pointer events: run only in an agreed slot, and look at the
// target's screen before the first step.
//   java -cp tigervnc-java-oculix-2.0.4.jar:oculixapi-4.0.0-linux.jar StepProbe.java HOST PASSWORD PORT TAP_LOG OUTDIR LABEL SCRIPT
// PORT is rfb_tap.py in front of the server; TAP_LOG is its log (what OculiX sent). SCRIPT is a text file with one
// step per line; after each step a capture is saved to OUTDIR as LABEL_NN.png. Steps:
//   look                      no input, only the capture
//   click X Y | rclick X Y | dclick X Y | hover X Y | move X Y
//   mdown ButtonL|MouseL|MouseM|MouseR        mouseDown with Button.LEFT or a Mouse.* constant
//   mup bare|MouseL|MouseM|MouseR|ALL         mouseUp(), mouseUp(Mouse.X), mouseUp(LEFT|MIDDLE|RIGHT)
//   drag X1 Y1 X2 Y2                          dragDrop
//   type TEXT                                 type(TEXT); "\r" in TEXT is Return, "\b" Backspace, "\t" Tab
//   env NAME                                  type the value of environment variable NAME, then Return
//   mod CTRL+SHIFT a                          type("a", KeyModifier.CTRL | KeyModifier.SHIFT)
//   wait MS
import java.io.File;
import java.io.RandomAccessFile;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.List;
import javax.imageio.ImageIO;
import org.sikuli.script.Button;
import org.sikuli.script.KeyModifier;
import org.sikuli.script.Location;
import org.sikuli.script.Mouse;
import org.sikuli.vnc.VNCScreen;

public class StepProbe {
    static RandomAccessFile tap;
    static long tapPos;

    // Summarise the RFB input messages the client sent since the last call.
    static String sent() throws Exception {
        byte[] b = new byte[(int) (tap.length() - tapPos)];
        tap.seek(tapPos);
        tap.readFully(b);
        tapPos += b.length;
        List<String> o = new ArrayList<>();
        int moves = 0;
        String last = null, mask = null;
        for (String line : new String(b).split("\n")) {
            String[] w = line.split(" ");
            if (w[0].equals("PointerEvent")) {
                String m = w[1].substring(5);
                if (m.equals(mask)) {
                    moves++;
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

    static int mouse(String name) {
        switch (name) {
            case "ButtonL": return Button.LEFT;
            case "MouseL": return Mouse.LEFT;
            case "MouseM": return Mouse.MIDDLE;
            case "MouseR": return Mouse.RIGHT;
            case "ALL": return Mouse.LEFT | Mouse.MIDDLE | Mouse.RIGHT;
            default: throw new IllegalArgumentException(name);
        }
    }

    static int modifiers(String names) {
        int m = 0;
        for (String n : names.split("\\+")) {
            switch (n) {
                case "CTRL": m |= KeyModifier.CTRL; break;
                case "SHIFT": m |= KeyModifier.SHIFT; break;
                case "ALT": m |= KeyModifier.ALT; break;
                case "META": m |= KeyModifier.META; break;
                default: throw new IllegalArgumentException(n);
            }
        }
        return m;
    }

    public static void main(String[] a) throws Exception {
        tap = new RandomAccessFile(a[3], "r");
        File out = new File(a[4]);
        out.mkdirs();
        String label = a[5];
        System.out.println("os " + System.getProperty("os.name") + ", java " + System.getProperty("java.vendor") + " "
                + System.getProperty("java.runtime.version") + ", script " + a[6]);
        // ON-VNC-1: limit the security types before the first start.
        Object p = Class.forName("com.tigervnc.rfb.SecurityClient").getField("secTypes").get(null);
        p.getClass().getMethod("setParam", String.class).invoke(p, "VncAuth,None");
        VNCScreen s = VNCScreen.start(a[0], Integer.parseInt(a[2]), a[1], 10, 1000);
        System.out.println("started, bounds " + s.getBounds() + ", image type " + s.capture().getImage().getType());
        Thread.sleep(500);
        tapPos = tap.length();
        int n = 0;
        for (String line : Files.readAllLines(new File(a[6]).toPath())) {
            if (line.isBlank() || line.startsWith("#")) continue;
            String[] w = line.trim().split(" ", 2);
            String[] arg = w.length > 1 ? w[1].split(" ") : new String[0];
            String result;
            try {
                Object r = "ok";
                switch (w[0]) {
                    case "look": break;
                    case "click": r = s.click(loc(s, arg, 0)); break;
                    case "rclick": r = s.rightClick(loc(s, arg, 0)); break;
                    case "dclick": r = s.doubleClick(loc(s, arg, 0)); break;
                    case "hover": r = s.hover(loc(s, arg, 0)); break;
                    case "move": r = s.mouseMove(loc(s, arg, 0)); break;
                    case "drag": r = s.dragDrop(loc(s, arg, 0), loc(s, arg, 2)); break;
                    case "mdown": s.mouseDown(mouse(arg[0])); break;
                    case "mup": if (arg[0].equals("bare")) s.mouseUp(); else s.mouseUp(mouse(arg[0])); break;
                    case "type": r = s.type(w[1].replace("\\r", "\r").replace("\\b", "\b").replace("\\t", "\t").replace("\\e", "\u001b")); break;
                    case "env": r = s.type(System.getenv(arg[0]) + "\r"); break;
                    case "mod": r = s.type(arg[1], modifiers(arg[0])); break;
                    case "wait": Thread.sleep(Long.parseLong(arg[0])); break;
                    default: throw new IllegalArgumentException(w[0]);
                }
                result = "returned " + r;
            } catch (Throwable e) {
                result = "THREW " + e;
            }
            Thread.sleep(700);
            String file = String.format("%s_%02d.png", label, ++n);
            ImageIO.write(s.capture().getImage(), "png", new File(out, file));
            String shown = w[0].equals("env") ? "env (text not shown)" : line.trim();
            System.out.printf("%s: %s%n    OculiX sent: %s%n    capture: %s%n", shown, result,
                    w[0].equals("env") ? "(not shown)" : sent(), file);
            if (w[0].equals("env")) sent();
        }
        s.stop();
        System.out.println("stopped");
        System.exit(0);
    }

    static Location loc(VNCScreen s, String[] arg, int i) {
        return s.newLocation(Integer.parseInt(arg[i]), Integer.parseInt(arg[i + 1]));
    }
}
