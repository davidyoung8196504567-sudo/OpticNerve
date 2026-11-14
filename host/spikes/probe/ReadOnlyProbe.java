// Read-only OculiX VNC probe: sends no pointer or key events. Written by the ON-UBUNTU24 session (2026-11-14) and
// run unchanged on both hosts, so the Windows and Ubuntu columns of docs/OCULIX_VNC_MATRIX.md are like for like.
//   java -cp oculixapi-4.0.0-linux.jar ReadOnlyProbe.java HOST PASSWORD OUTDIR
// On Linux, OCR needs LD_LIBRARY_PATH set to Legerix's folder (docs/PLAN.md, OculiX issues). The pattern rectangle
// (470,75,360,35) and the colour points assume the terminal with the welcome message is on screen.
import java.awt.image.BufferedImage;
import java.io.File;
import javax.imageio.ImageIO;
import org.sikuli.script.Finder;
import org.sikuli.script.Match;
import org.sikuli.script.OCR;
import org.sikuli.script.Pattern;
import org.sikuli.script.Region;
import org.sikuli.vnc.VNCScreen;

public class ReadOnlyProbe {
    static double since(long t) {
        return (System.nanoTime() - t) / 1e9;
    }

    static String describe(BufferedImage img) {
        return String.format("%dx%d, image type %d, %s / %s", img.getWidth(), img.getHeight(), img.getType(),
                img.getColorModel().getClass().getSimpleName(), img.getRaster().getDataBuffer().getClass().getSimpleName());
    }

    static BufferedImage toBgr(BufferedImage src) {
        BufferedImage dst = new BufferedImage(src.getWidth(), src.getHeight(), BufferedImage.TYPE_3BYTE_BGR);
        dst.getGraphics().drawImage(src, 0, 0, null);
        return dst;
    }

    interface Step {
        Object run() throws Throwable;
    }

    static void step(String name, Step s) {
        long t = System.nanoTime();
        try {
            Object r = s.run();
            System.out.printf("%s: OK in %.3f s: %s%n", name, since(t), r);
        } catch (Throwable e) {
            StackTraceElement[] st = e.getStackTrace();
            System.out.printf("%s: FAILS in %.3f s: %s at %s%n", name, since(t), e, st.length > 0 ? st[0] : "?");
        }
    }

    public static void main(String[] a) throws Exception {
        String host = a[0], password = a[1];
        File out = new File(a[2]);
        out.mkdirs();
        System.out.println("os " + System.getProperty("os.name") + ", java " + System.getProperty("java.version"));

        long t0 = System.nanoTime();
        VNCScreen screen = VNCScreen.start(host, 5900, password, 10, 1000);
        System.out.printf("start: %.2f s, running %s, bounds %s%n", since(t0), screen.isRunning(), screen.getBounds());

        BufferedImage[] full = new BufferedImage[1];
        for (int i = 0; i < 3; i++) {
            int n = i;
            step("capture() #" + (i + 1), () -> {
                BufferedImage img = screen.capture().getImage();
                if (n == 0) full[0] = img;
                return describe(img);
            });
        }
        ImageIO.write(full[0], "png", new File(out, "full.png"));

        step("capture(100,100,400,300)", () -> {
            BufferedImage img = screen.capture(100, 100, 400, 300).getImage();
            ImageIO.write(img, "png", new File(out, "region.png"));
            BufferedImage ref = full[0].getSubimage(100, 100, 400, 300);
            int diff = 0;
            for (int y = 0; y < 300; y++)
                for (int x = 0; x < 400; x++)
                    if ((img.getRGB(x, y) & 0xffffff) != (ref.getRGB(x, y) & 0xffffff)) diff++;
            return describe(img) + ", pixels differing from the full capture: " + diff;
        });

        for (int[] p : new int[][] {{10, 10}, {484, 90}, {485, 92}, {486, 94}, {198, 1052}}) {
            step("Location(" + p[0] + "," + p[1] + ").getColor", () -> {
                java.awt.Color c = screen.newLocation(p[0], p[1]).getColor();
                return c + ", capture pixel " + new java.awt.Color(full[0].getRGB(p[0], p[1]));
            });
        }

        // Pattern cut from the first capture, searched in a later one.
        BufferedImage bgr = toBgr(full[0]);
        int px = 470, py = 75, pw = 360, ph = 35;
        File pattern = new File(out, "pattern.png");
        ImageIO.write(bgr.getSubimage(px, py, pw, ph), "png", pattern);
        System.out.printf("pattern: %dx%d cut at (%d,%d), centre (%d,%d)%n", pw, ph, px, py, px + pw / 2, py + ph / 2);

        step("VNCScreen.exists(Pattern)", () -> screen.exists(new Pattern(pattern.getPath()).similar(0.8), 0));
        step("VNCScreen.find(Pattern)", () -> screen.find(new Pattern(pattern.getPath()).similar(0.8)));
        step("VNCScreen.findAll(Pattern)", () -> {
            java.util.Iterator<Match> it = screen.findAll(new Pattern(pattern.getPath()).similar(0.8));
            int n = 0;
            while (it.hasNext()) { it.next(); n++; }
            return n + " matches";
        });

        BufferedImage later = toBgr(screen.capture().getImage());
        for (double similarity : new double[] {0.99, 0.8}) {
            step(String.format("Finder on BGR copy at %.2f", similarity), () -> {
                Finder f = new Finder(later);
                f.find(new Pattern(pattern.getPath()).similar(similarity));
                Match m = f.hasNext() ? f.next() : null;
                f.destroy();
                return m == null ? "not found" : String.format("score %.4f, target %s", m.getScore(), m.getTarget());
            });
        }

        step("OCR.readText(BGR copy)", () -> {
            String text = OCR.readText(later);
            java.nio.file.Files.writeString(new File(out, "ocr_bgr.txt").toPath(), text);
            return text.length() + " chars, " + text.split("\n").length + " lines";
        });
        step("OCR.readText(raw capture)", () -> OCR.readText(full[0]).length() + " chars");
        step("VNCScreen.text()", () -> {
            String text = screen.text();
            java.nio.file.Files.writeString(new File(out, "ocr_screen.txt").toPath(), text);
            return text.length() + " chars";
        });
        step("Region(100,100,800,400).text()", () -> {
            Region r = screen.newRegion(100, 100, 800, 400);
            return r.text().length() + " chars";
        });
        step("waitForScreenStable(3000,500)", () -> screen.waitForScreenStable(3000, 500));

        screen.stop();
        System.out.println("stopped");
        System.exit(0);
    }
}
