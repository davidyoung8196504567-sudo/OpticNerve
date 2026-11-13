// Grab frames from a webcam pointed at the target's monitor, with the OpenCV that OculiX's jar bundles.
//
//   java -cp oculixapi-4.0.0-windows.jar host/tools/webcam/WebcamGrab.java INDEX OUT_PREFIX [COUNT [INTERVAL_MS]]
//
// Writes OUT_PREFIX_1.png ... Uses Media Foundation at 1920 x 1080. The first frames after opening are dropped while the
// camera adjusts exposure. Set OPENCV_VIDEOIO_MSMF_ENABLE_HW_TRANSFORMS=0 first: without it, opening the Logitech C922
// hung (OpenCV 4.10 in OculiX 4.0.0, Windows 11, 2026-11-13). DirectShow by index is not available in that build.
import nu.pattern.OpenCV;
import org.opencv.core.Mat;
import org.opencv.imgcodecs.Imgcodecs;
import org.opencv.videoio.VideoCapture;
import org.opencv.videoio.Videoio;

public class WebcamGrab {
    public static void main(String[] a) throws Exception {
        OpenCV.loadLocally();
        int index = Integer.parseInt(a[0]);
        int count = a.length > 2 ? Integer.parseInt(a[2]) : 1;
        int interval = a.length > 3 ? Integer.parseInt(a[3]) : 1000;
        VideoCapture cam = new VideoCapture(index, Videoio.CAP_MSMF);
        if (!cam.isOpened()) {
            System.out.println("camera " + index + " did not open");
            System.exit(1);
        }
        cam.set(Videoio.CAP_PROP_FRAME_WIDTH, 1920);
        cam.set(Videoio.CAP_PROP_FRAME_HEIGHT, 1080);
        Mat m = new Mat();
        for (int i = 0; i < 30; i++) cam.read(m);
        for (int i = 1; i <= count; i++) {
            long t = System.currentTimeMillis();
            cam.read(m);
            String out = a[1] + "_" + i + ".png";
            Imgcodecs.imwrite(out, m);
            System.out.printf("%s %dx%d%n", out, m.cols(), m.rows());
            if (i < count) Thread.sleep(Math.max(0, interval - (System.currentTimeMillis() - t)));
        }
        cam.release();
        System.exit(0);
    }
}
