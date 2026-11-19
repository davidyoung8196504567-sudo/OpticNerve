# A new OculiX IDE test for the Pi's screen. Copy this folder, then rename the folder and this file to the test's
# name. Needs ON_HOST and ON_VNC_PASSWORD in the environment the IDE was started from (see ../README.md).
from java.lang import System, Class
from org.sikuli.vnc import VNCScreen

host = System.getenv("ON_HOST")
password = System.getenv("ON_VNC_PASSWORD")
if not host or not password:
    raise Exception("set ON_HOST and ON_VNC_PASSWORD before starting the IDE")
# Offer only VNC authentication (configuration ON-VNC-1)
Class.forName("com.tigervnc.rfb.SecurityClient").getField("secTypes").get(None).setParam("VncAuth,None")
vnc = VNCScreen.start(host, 5901, password, 10, 1000)

failed = []
def check(name, ok, detail=""):
    print("%s %s %s" % ("PASS" if ok else "FAIL", name, detail))
    if not ok:
        failed.append(name)

try:
    # The test. One capture per line, for example:
    #   tile = <capture>
    #   m = vnc.exists(tile, 5)
    #   check("the tile is shown", m is not None)
    #   vnc.click(tile)
    pass

    # Leave the screen as you found it: undo what the test did, then check the start state again.
finally:
    vnc.stop()

print("ALL PASSED" if not failed else "%d FAILED: %s" % (len(failed), ", ".join(failed)))
