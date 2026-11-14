// Unboxing scenario (docs/PLAN.md): capture the Pi's screen through OculiX's VNCScreen and OCR it with OculiX's
// bundled Tesseract.
//
//   java -cp oculixapi-4.0.0-windows.jar host/spikes/ocr/OcrScreen.java HOST PASSWORD OUT.png OUT.txt
//   java -cp oculixapi-4.0.0-windows.jar host/spikes/ocr/OcrScreen.java --file IN.png OUT.txt
//
// The capture is copied into a TYPE_3BYTE_BGR image first (OculiX 4.0.0 VNC captures are TYPE_CUSTOM, see
// docs/upstream/oculix-vncscreen-find-classcast.md).
import java.awt.image.BufferedImage;
import java.io.File;
import java.nio.file.Files;
import javax.imageio.ImageIO;
import org.sikuli.script.OCR;
import org.sikuli.vnc.VNCScreen;

public class OcrScreen {
    static BufferedImage toBgr(BufferedImage src) {
        BufferedImage dst = new BufferedImage(src.getWidth(), src.getHeight(), BufferedImage.TYPE_3BYTE_BGR);
        dst.getGraphics().drawImage(src, 0, 0, null);
        return dst;
    }

    public static void main(String[] a) throws Exception {
        BufferedImage img;
        String txt;
        if (a[0].equals("--file")) {
            img = toBgr(ImageIO.read(new File(a[1])));
            txt = a[2];
        } else {
            com.tigervnc.rfb.SecurityClient.secTypes.setParam("VncAuth,None"); // ON-VNC-1: never Apple's DH login
            VNCScreen screen = VNCScreen.start(a[0], 5900, a[1], 10, 1000);
            img = toBgr(screen.capture().getImage());
            screen.stop();
            ImageIO.write(img, "png", new File(a[2]));
            txt = a[3];
        }
        long t = System.nanoTime();
        String text = OCR.readText(img);
        System.out.printf("OCR: %d characters in %.2f s%n", text.length(), (System.nanoTime() - t) / 1e9);
        Files.writeString(new File(txt).toPath(), text);
        System.exit(0);
    }
}
