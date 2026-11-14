// Read-only: which RFB encodings OculiX's VNC client actually received. Sends no pointer or key events.
//   java -cp oculixapi-4.0.0-windows.jar EncodingProbe.java HOST PASSWORD [PORT, default 5900]
// TigerVNC's CMsgReader creates one Decoder per encoding the first time a rectangle of that encoding arrives, so the
// non-null slots of its decoders array are the encodings the server used (0 raw, 5 Hextile, 7 Tight, 16 ZRLE).
import com.sikulix.vnc.VNCClient;
import java.lang.reflect.Field;
import org.sikuli.vnc.VNCScreen;

public class EncodingProbe {
    static Object field(Object o, Class<?> c, String name) throws Exception {
        Field f = c.getDeclaredField(name);
        f.setAccessible(true);
        return f.get(o);
    }

    public static void main(String[] a) throws Exception {
        int port = a.length > 2 ? Integer.parseInt(a[2]) : 5900;
        VNCScreen screen = VNCScreen.start(a[0], port, a[1], 10, 1000);
        for (int i = 0; i < 3; i++) {
            screen.capture();
            Thread.sleep(1000);
        }
        VNCClient client = screen.getClient();
        System.out.println("currentEncoding (preferred, sent to the server) " + field(client, VNCClient.class, "currentEncoding"));
        Object reader = client.reader();
        Object[] decoders = (Object[]) field(reader, com.tigervnc.rfb.CMsgReader.class, "decoders");
        for (int e = 0; e < decoders.length; e++)
            if (decoders[e] != null) System.out.println("received encoding " + e + ": " + decoders[e].getClass().getSimpleName());
        System.out.println("pixel format " + client.cp.pf().print());
        screen.stop();
        System.exit(0);
    }
}
