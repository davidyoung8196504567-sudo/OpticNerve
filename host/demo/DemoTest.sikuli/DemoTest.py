# The OpticNerve demo test for the OculiX IDE: the same checks as host/demo/DemoTest.java, written as an OculiX
# (Jython) script whose images live in this folder. Open the folder in the OculiX IDE and press Run, or run it
# without the IDE window:
#
#   java -cp "tigervnc-java-oculix-2.0.4.jar;oculixide-4.0.0-windows.jar" org.sikuli.ide.Sikulix -r host/demo/DemoTest.sikuli
#
# Needs demo_panel showing on the Pi, and one of these in the environment the IDE was started from:
#   - ON_HOST and ON_VNC_PASSWORD: the script connects to the agent (port 5901) and works on the Pi's screen directly.
#     Configuration ON-VNC-1: the tigervnc-java-oculix jar first on the classpath, security types VncAuth,None.
#   - ON_LOCAL_SCREEN=N: the script works on this computer's monitor N (as OculiX numbers them, 0 = main), where a
#     VNC viewer shows the Pi full screen at 100 % scale; its clicks and keys reach the Pi through the viewer.
#
# Every call goes to `scr`, the screen chosen above. A bare click("plus.png") would search and click this computer's
# main screen.
from java.lang import System, Class
from org.sikuli.vnc import VNCScreen

vnc = None
local = System.getenv("ON_LOCAL_SCREEN")
if local:
    scr = Screen(int(local))
else:
    host = System.getenv("ON_HOST")
    password = System.getenv("ON_VNC_PASSWORD")
    if not host or not password:
        raise Exception("set ON_HOST and ON_VNC_PASSWORD (or ON_LOCAL_SCREEN) before starting the IDE")
    # Offer only VNC authentication (ON-VNC-1)
    Class.forName("com.tigervnc.rfb.SecurityClient").getField("secTypes").get(None).setParam("VncAuth,None")
    vnc = VNCScreen.start(host, 5901, password, 10, 1000)
    scr = vnc

# Positions on the panel, from the panel's top left corner. The panel's corner is found from the RESET button (at
# 750,700 on the panel), since a viewer may not show the panel exactly at its monitor's corner.
ox, oy = 0, 0
def area(x, y, w, h):
    return Region(ox + x, oy + y, w, h) if vnc is None else vnc.newRegion(x, y, w, h)

# A click that holds the button 50 ms, as a hand does. OculiX's default is 0, so the press and the release go out
# together, and some applications miss such clicks. OculiX resets ClickDelay after each click, so it is set every time.
def click_on(image):
    Settings.ClickDelay = 0.05
    scr.click(image)

failed = []
def check(name, ok, detail):
    print("%s %s (%s)" % ("PASS" if ok else "FAIL", name, detail))
    if not ok:
        failed.append(name)

# Each state is checked by its picture, as a test made in the IDE would: the lamp's picture must be there, and the
# other colour's picture must not.
def shows(box, want, other, wait):
    m = box.exists(Pattern(want).similar(0.95), wait)
    return m is not None and box.exists(Pattern(other).similar(0.95), 0) is None, \
        "score %.4f" % m.getScore() if m else "not found"

try:
    m = scr.exists(Pattern("reset.png").similar(0.90), 3)
    check("find the RESET button by image", m is not None, "score %.4f" % m.getScore() if m else "not found")
    if m:
        ox, oy = m.x - 750, m.y - 700
        if vnc is None:
            print("the panel's corner is at %d,%d on this computer's screens" % (ox, oy))
    count_box = area(160, 280, 700, 180)
    lamp_box = area(1080, 260, 540, 220)
    click_on("reset.png")
    m = count_box.exists(Pattern("count_0.png").similar(0.95), 2)
    check("after RESET the count shows 0", m is not None, "score %.4f" % m.getScore() if m else "not found")
    ok, detail = shows(lamp_box, "lamp_green.png", "lamp_red.png", 2)
    check("after RESET the lamp is green", ok, detail)

    for i in range(3):
        click_on("plus.png")
        sleep(0.5)
    m = count_box.exists(Pattern("count_3.png").similar(0.95), 2)
    check("after three clicks on +1 the count shows 3", m is not None, "score %.4f" % m.getScore() if m else "not found")

    click_on("lamp.png")
    ok, detail = shows(lamp_box, "lamp_red.png", "lamp_green.png", 2)
    check("after LAMP the lamp is red", ok, detail)

    scr.type("\r")  # Return clears the text line (Enter as "\r", ON-VNC-1)
    sleep(0.5)
    scr.type("hello pi 5")
    sleep(0.5)
    text = " ".join(area(400, 505, 1360, 80).text().split())
    check("typed text appears on the panel", text == "HELLO PI 5", 'OCR read "%s"' % text)

    # A run leaves the panel as it found it, so runs can follow one another
    click_on("reset.png")
    count0 = count_box.exists(Pattern("count_0.png").similar(0.95), 2)
    green, detail = shows(lamp_box, "lamp_green.png", "lamp_red.png", 2)
    left = " ".join(area(400, 505, 1360, 80).text().split())
    check("the run leaves the panel as it started", count0 is not None and green and left == "",
          'count %s, lamp %s, text "%s"' % ("0" if count0 else "not 0", "green" if green else "not green", left))
finally:
    if vnc:
        vnc.stop()

print("ALL PASSED" if not failed else "%d FAILED: %s" % (len(failed), ", ".join(failed)))
