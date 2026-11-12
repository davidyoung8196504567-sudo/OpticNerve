// Spike S1 (docs/DESIGN.md): does OculiX's VNCScreen connect to QNX's vncserv, see the screen and click?
//
//   java -cp oculixapi-4.0.0-windows.jar host/spikes/s1/S1VncScreen.java HOST PASSWORD PATTERN.png OUTDIR
//
// Connects with VNCScreen.start, times a few full captures, saves the first, looks for PATTERN (the green
// "unlock" dot of Weston's lock screen), clicks it if found, and saves a capture taken after the click.
import java.io.File;
import javax.imageio.ImageIO;
import org.sikuli.script.Match;
import org.sikuli.script.Pattern;
import org.sikuli.script.ScreenImage;
import org.sikuli.vnc.VNCScreen;

public class S1VncScreen {
    public static void main(String[] a) throws Exception {
        String host = a[0], password = a[1], pattern = a[2];
        File out = new File(a[3]);
        out.mkdirs();

        long t0 = System.nanoTime();
        VNCScreen screen = VNCScreen.start(host, 5900, password, 10, 1000);
        System.out.printf("start: %.2f s, running %s, bounds %s, id %s%n", (System.nanoTime() - t0) / 1e9,
                screen.isRunning(), screen.getBounds(), screen.getIDString());

        ScreenImage first = null;
        for (int i = 0; i < 3; i++) {
            long t = System.nanoTime();
            ScreenImage img = screen.capture();
            System.out.printf("capture %d: %.3f s, %dx%d%n", i, (System.nanoTime() - t) / 1e9,
                    img.getImage().getWidth(), img.getImage().getHeight());
            if (first == null) first = img;
        }
        ImageIO.write(first.getImage(), "png", new File(out, "s1_before.png"));

        for (double similarity : new double[] {0.95, 0.9, 0.8}) {
            long t = System.nanoTime();
            Match m = screen.exists(new Pattern(pattern).similar(similarity), 0);
            System.out.printf("find at similarity %.2f: %s in %.2f s%n", similarity,
                    m == null ? "not found" : String.format("score %.4f at %s", m.getScore(), m.getTarget()),
                    (System.nanoTime() - t) / 1e9);
        }
        Match m = screen.exists(new Pattern(pattern).similar(0.8), 0);
        if (m != null) {
            screen.click(m);
            System.out.println("clicked " + m.getTarget());
            Thread.sleep(2000);
            ImageIO.write(screen.capture().getImage(), "png", new File(out, "s1_after_click.png"));
        }
        screen.stop();
        System.out.println("stopped");
        System.exit(0);
    }
}
