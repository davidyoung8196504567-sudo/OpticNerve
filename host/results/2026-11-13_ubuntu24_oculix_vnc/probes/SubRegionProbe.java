// Can VNCScreen.capture(x,y,w,h) return stale pixels? Read-only on the VNC side: sends no pointer or key events.
//   java -cp <classpath> SubRegionProbe.java HOST PASSWORD PORT X_DISPLAY
// X_DISPLAY is the X display behind the VNC server (same machine). For each region and round the probe changes
// the whole root window to a new solid colour with xsetroot, waits, then takes capture(x,y,w,h) FIRST and a full
// capture() right after. The screen is static between the two, so a pixel of the region capture that differs from
// the full capture is stale. Background: VNCClient.refreshFramebuffer builds Rect(x, y, w, h), but Rect takes
// corners, so the update request asks for (w - x) by (h - y) at x,y.
import java.awt.image.BufferedImage;
import org.sikuli.vnc.VNCScreen;

public class SubRegionProbe {
    static void root(String display, int rgb) throws Exception {
        new ProcessBuilder("xsetroot", "-display", display, "-solid", String.format("#%06x", rgb)).inheritIO().start().waitFor();
    }

    public static void main(String[] a) throws Exception {
        String display = a[3];
        System.out.println("os " + System.getProperty("os.name") + ", java " + System.getProperty("java.vendor") + " "
                + System.getProperty("java.runtime.version"));
        root(display, 0x000000);
        VNCScreen s = VNCScreen.start(a[0], Integer.parseInt(a[2]), a[1], 10, 1000);
        System.out.println("started, bounds " + s.getBounds() + ", image type " + s.capture().getImage().getType());
        int[][] regions = {
            {100, 100, 400, 300},   // w > x, h > y: request 300x200, smaller than the region
            {0, 0, 500, 500},       // x = y = 0: request equals the region
            {300, 200, 300, 200},   // w = x, h = y: request 0x0
            {300, 100, 300, 400},   // w = x only: request 0x300
            {1000, 600, 400, 300},  // w < x, h < y: request wraps to a huge size, server clips
            {200, 600, 500, 300},   // w > x, h < y
            {50, 50, 1800, 1000},   // nearly the whole screen: request 1750x950
        };
        int[] colours = {0xff0000, 0x00ff00, 0x0000ff, 0xffffff, 0x808080, 0x000000};
        int round = 0;
        for (int[] r : regions) {
            int staleRounds = 0, maxStale = 0;
            StringBuilder detail = new StringBuilder();
            for (int i = 0; i < colours.length; i++) {
                root(display, colours[(round++) % colours.length]);
                Thread.sleep(400);
                long t = System.nanoTime();
                BufferedImage part = s.capture(r[0], r[1], r[2], r[3]).getImage();
                double secs = (System.nanoTime() - t) / 1e9;
                BufferedImage full = s.capture().getImage();
                int stale = 0, minX = r[2], minY = r[3], maxX = -1, maxY = -1;
                for (int y = 0; y < r[3]; y++)
                    for (int x = 0; x < r[2]; x++)
                        if ((part.getRGB(x, y) & 0xffffff) != (full.getRGB(r[0] + x, r[1] + y) & 0xffffff)) {
                            stale++;
                            minX = Math.min(minX, x); maxX = Math.max(maxX, x);
                            minY = Math.min(minY, y); maxY = Math.max(maxY, y);
                        }
                if (stale > 0) {
                    staleRounds++;
                    maxStale = Math.max(maxStale, stale);
                    detail.append(String.format(" [round %d: %d stale, in region x %d..%d y %d..%d, %.2f s]", i + 1, stale, minX, maxX, minY, maxY, secs));
                }
            }
            System.out.printf("capture(%d,%d,%d,%d), %d px: stale in %d of %d rounds, max %d px%s%n", r[0], r[1], r[2], r[3],
                    r[2] * r[3], staleRounds, colours.length, maxStale, detail);
        }
        s.stop();
        System.out.println("stopped");
        System.exit(0);
    }
}
