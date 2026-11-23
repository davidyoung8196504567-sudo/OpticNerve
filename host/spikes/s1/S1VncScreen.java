// Spike S1: does OculiX's VNCScreen connect to QNX's vncserv, see the screen and click?
//
//   java -cp oculixapi-4.0.0-windows.jar host/spikes/s1/S1VncScreen.java HOST PASSWORD capture OUT.png
//   java -cp oculixapi-4.0.0-windows.jar host/spikes/s1/S1VncScreen.java HOST PASSWORD find PATTERN.png OUTDIR [click]
//
// capture: connect with VNCScreen.start, time three full captures, save the first.
// find: try Region.exists on the VNCScreen (fails in OculiX 4.0.0, see
// docs/upstream/oculix-vncscreen-find-classcast.md), then the workaround: copy the capture into a TYPE_3BYTE_BGR
// image, run a Finder on it, and with "click" click the match through the VNCScreen, by hand (see glide below),
// and save a capture taken 2 s later.
import java.awt.image.BufferedImage;
import java.io.File;
import javax.imageio.ImageIO;
import org.sikuli.script.Finder;
import org.sikuli.script.Match;
import org.sikuli.script.Pattern;
import org.sikuli.vnc.VNCScreen;

public class S1VncScreen {
    static VNCScreen s;

    // A hand on the mouse and keyboard: the one way this test sends input (the model of host/tools/hand.py; the same
    // block in each Java test, kept identical by tests/regression/test_hand.py). The pointer glides from where it is
    // along a slightly curved path, easing in and out, at about 60 pointer events a second (0.5 to 0.8 s by distance,
    // about 1.5 px of tremor), rests 80 to 200 ms, then clicks with the button held 50 to 90 ms: OculiX's click, with
    // MoveMouseDelay 0 so that OculiX adds no move of its own, and ClickDelay set each time (OculiX resets it after
    // each click; with 0 the press and release go out together). A double or triple click holds each press 50 to
    // 90 ms, 80 to 150 ms apart. A drag holds the button along such a path. Each key is held 40 to 80 ms and comes 60
    // to 180 ms after the last, one character at a time (typed at full speed, a Developer Desktop application got some
    // characters out of order and lost others, 2026-11-17); wheel notches come 60 to 150 ms apart. The agent does not
    // report where the pointer is, so the first move of a run starts at the bottom centre of the screen.
    static final java.util.Random hand = new java.util.Random();
    static int handX = -1, handY = -1;
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
        rest(0.08, 0.2);
        org.sikuli.basics.Settings.ClickDelay = 0.05 + hand.nextDouble() * 0.04;
        s.click(s.newLocation(handX, handY));
    }

    // A double click (n = 2) or a triple click (n = 3), each press held 50 to 90 ms, 80 to 150 ms apart.
    static void handClicks(int x, int y, int n) throws Exception {
        glide(x, y);
        rest(0.08, 0.2);
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
        rest(0.08, 0.2);
        s.getRobot().mouseDown(org.sikuli.script.Mouse.LEFT);
        try {
            rest(0.1, 0.2);
            glide(x2, y2);
            rest(0.08, 0.2);
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
    // (end of the hand)

    static double since(long t) {
        return (System.nanoTime() - t) / 1e9;
    }

    static BufferedImage capture(VNCScreen screen) {
        long t = System.nanoTime();
        BufferedImage img = screen.capture().getImage();
        System.out.printf("capture: %.3f s, %dx%d, image type %d%n", since(t), img.getWidth(), img.getHeight(),
                img.getType());
        return img;
    }

    static BufferedImage toBgr(BufferedImage src) {
        BufferedImage dst = new BufferedImage(src.getWidth(), src.getHeight(), BufferedImage.TYPE_3BYTE_BGR);
        dst.getGraphics().drawImage(src, 0, 0, null);
        return dst;
    }

    public static void main(String[] a) throws Exception {
        String host = a[0], password = a[1], mode = a[2];
        com.tigervnc.rfb.SecurityClient.secTypes.setParam("VncAuth,None"); // ON-VNC-1: never Apple's DH login
        long t0 = System.nanoTime();
        VNCScreen screen = VNCScreen.start(host, 5900, password, 10, 1000);
        s = screen;
        System.out.printf("start: %.2f s, running %s, bounds %s%n", since(t0), screen.isRunning(), screen.getBounds());

        if (mode.equals("capture")) {
            BufferedImage first = capture(screen);
            capture(screen);
            capture(screen);
            ImageIO.write(first, "png", new File(a[3]));
        } else {
            String pattern = a[3];
            File out = new File(a[4]);
            out.mkdirs();
            boolean click = a.length > 5 && a[5].equals("click");
            try {
                Match m = screen.exists(new Pattern(pattern).similar(0.8), 0);
                System.out.println("VNCScreen.exists: " + m);
            } catch (ClassCastException e) {
                System.out.println("VNCScreen.exists: " + e.getClass().getSimpleName() + " (OculiX bug, as before)");
            }
            BufferedImage img = toBgr(capture(screen));
            Match best = null;
            for (double similarity : new double[] {0.99, 0.95, 0.9, 0.8}) {
                long t = System.nanoTime();
                Finder f = new Finder(img);
                f.find(new Pattern(pattern).similar(similarity));
                Match m = f.hasNext() ? f.next() : null;
                System.out.printf("Finder at similarity %.2f: %s in %.2f s%n", similarity,
                        m == null ? "not found" : String.format("score %.4f, target %s", m.getScore(), m.getTarget()),
                        since(t));
                if (best == null) best = m;
                f.destroy();
            }
            if (click && best != null) {
                handClick(best.getTarget().x, best.getTarget().y);
                System.out.println("clicked " + best.getTarget());
                Thread.sleep(2000);
                ImageIO.write(capture(screen), "png", new File(out, "s1_after_click.png"));
            }
        }
        screen.stop();
        System.out.println("stopped");
        System.exit(0);
    }
}
