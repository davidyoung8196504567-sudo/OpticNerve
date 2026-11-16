// OculiX input probe against the OpticNerve server: SENDS key and pointer events. Each step is one OculiX call,
// printed with its result; what arrives is read from the target test application's log (agent/spikes/input_log.c,
// a full-screen window), so the server's side and the client's side can be compared step by step.
//   java -cp tigervnc-java-oculix-2.0.4.jar;oculixapi-4.0.0-windows.jar OculixInputProbe.java HOST PASSWORD PORT
// Configuration ON-VNC-1: the tigervnc-java-oculix 2.0.4 jar first, security types limited to VncAuth,None before
// the first start, Enter as type("\r"), Mouse.* constants only. Key.ENTER is sent once on purpose: it sends
// Linefeed, which the server must not turn into Return (the guardrail), so it should arrive as nothing.
import org.sikuli.script.Key;
import org.sikuli.script.KeyModifier;
import org.sikuli.script.Location;
import org.sikuli.script.Mouse;
import org.sikuli.vnc.VNCScreen;

public class OculixInputProbe {
    interface Step {
        Object run() throws Throwable;
    }

    static void step(String name, Step st) {
        long t0 = System.nanoTime();
        String r;
        try {
            r = String.valueOf(st.run());
        } catch (Throwable e) {
            r = "THREW " + e;
        }
        System.out.printf("%-48s %-12s %6.0f ms%n", name, r, (System.nanoTime() - t0) / 1e6);
        try {
            Thread.sleep(300);
        } catch (InterruptedException ignored) {
        }
    }

    public static void main(String[] a) throws Exception {
        Object p = Class.forName("com.tigervnc.rfb.SecurityClient").getField("secTypes").get(null);
        p.getClass().getMethod("setParam", String.class).invoke(p, "VncAuth,None");
        VNCScreen s = VNCScreen.start(a[0], Integer.parseInt(a[2]), a[1], 10, 1000);
        Location l1 = s.newLocation(300, 200), l2 = s.newLocation(640, 480), l3 = s.newLocation(900, 600);
        step("mouseMove(300,200)", () -> s.mouseMove(l1));
        step("click(640,480)", () -> s.click(l2));
        step("doubleClick(640,480)", () -> s.doubleClick(l2));
        step("rightClick(900,600)", () -> s.rightClick(l3));
        step("dragDrop(300,200 -> 900,600)", () -> s.dragDrop(l1, l3));
        step("mouseDown(Mouse.LEFT) mouseUp(Mouse.LEFT) at 640,480", () -> {
            s.mouseMove(l2);
            s.mouseDown(Mouse.LEFT);
            s.mouseUp(Mouse.LEFT);
            return "ok";
        });
        step("wheel(640,480, Mouse.WHEEL_DOWN, 3)", () -> s.wheel(l2, Mouse.WHEEL_DOWN, 3));
        step("type(\"abc XYZ 123\\r\")", () -> s.type("abc XYZ 123\r"));
        step("type(\"!@#-=[]\")", () -> s.type("!@#-=[]"));
        step("type(Key.ENTER) [guardrail: Linefeed]", () -> s.type(Key.ENTER));
        step("type(Key.BACKSPACE)", () -> s.type(Key.BACKSPACE));
        step("type(Key.F5)", () -> s.type(Key.F5));
        step("type(\"c\", KeyModifier.CTRL)", () -> s.type("c", KeyModifier.CTRL));
        step("type(\"t\", KeyModifier.ALT)", () -> s.type("t", KeyModifier.ALT));
        step("capture() after input", () -> s.capture().getImage().getWidth());
        s.stop();
        System.out.println("stopped");
    }
}
