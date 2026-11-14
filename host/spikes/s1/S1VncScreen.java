// Spike S1 (docs/DESIGN.md): does OculiX's VNCScreen connect to QNX's vncserv, see the screen and click?
//
//   java -cp oculixapi-4.0.0-windows.jar host/spikes/s1/S1VncScreen.java HOST PASSWORD capture OUT.png
//   java -cp oculixapi-4.0.0-windows.jar host/spikes/s1/S1VncScreen.java HOST PASSWORD find PATTERN.png OUTDIR [click]
//
// capture: connect with VNCScreen.start, time three full captures, save the first.
// find: try Region.exists on the VNCScreen (fails in OculiX 4.0.0, see docs/notes/2026-11-12_spikes.md), then the
// workaround: copy the capture into a TYPE_3BYTE_BGR image, run a Finder on it, and with "click" click the match
// through the VNCScreen and save a capture taken 2 s later.
import java.awt.image.BufferedImage;
import java.io.File;
import javax.imageio.ImageIO;
import org.sikuli.script.Finder;
import org.sikuli.script.Match;
import org.sikuli.script.Pattern;
import org.sikuli.vnc.VNCScreen;

public class S1VncScreen {
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
                screen.click(best.getTarget());
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
