# The OpticNerve demo test for the OculiX IDE: the same checks as host/demo/DemoTest.java, written as an OculiX
# (Jython) script whose images live in this folder. Open the folder in the OculiX IDE and press Run, or run it
# without the IDE window:
#
#   java -cp "tigervnc-java-oculix-2.0.4.jar;oculixide-4.0.0-windows.jar" org.sikuli.ide.Sikulix -r host/demo/DemoTest.sikuli
#
# Needs demo_panel showing on the Pi, and one of these in the environment the IDE was started from or in the
# settings file (~/.config/opticnerve/env):
#   - ON_HOST and ON_VNC_PASSWORD: the script connects to the agent (port ON_VNC_PORT, default 5901) and works on the
#     Pi's screen directly.
#     Configuration ON-VNC-1: the tigervnc-java-oculix jar first on the classpath, security types VncAuth,None.
#   - ON_LOCAL_SCREEN=N: the script works on this computer's monitor N (as OculiX numbers them, 0 = main), where a
#     VNC viewer shows the Pi full screen at 100 % scale; its clicks and keys reach the Pi through the viewer.
#
# The pointer jumps straight to each target; ON_HAND=1 (in the environment or the settings file) makes it glide the
# way a hand does, as host/demo/DemoTest.java's -hand does. Keys always keep a hand's pace.
#
# Every call goes to `scr`, the screen chosen above. A bare click("plus.png") would search and click this computer's
# main screen.
from java.lang import System, Class
from org.sikuli.vnc import VNCScreen
import os, re
def cfg(name, default=None):
    # A setting: the environment the IDE was started from, then the settings file ON_CONFIG (default
    # ~/.config/opticnerve/env; template host/tools/opticnerve.env.example), then the default.
    v = System.getenv(name)
    if v:
        return v
    f = System.getenv("ON_CONFIG") or os.path.join(System.getProperty("user.home"), ".config", "opticnerve", "env")
    if os.path.isfile(f):
        for line in open(f):
            # the grammar of host/tools/on_env.sh: [export] NAME=VALUE [# comment], VALUE taken literally
            m = re.match(r"""\s*(?:export\s+)?%s=(?:'([^']*)'|"([^"]*)"|([^\s#'"]*))\s*(?:#.*)?$""" % name,
                         line.rstrip("\r\n"))
            if m and (m.group(1) or m.group(2) or m.group(3)):
                return m.group(1) or m.group(2) or m.group(3)
    return default

vnc = None
local = System.getenv("ON_LOCAL_SCREEN")
if local:
    scr = Screen(int(local))
    # the viewer shows the agent's pointer arrow (ON-AG-001), which this screen's image searches would see
    print("ON_LOCAL_SCREEN: switch the agent's pointer arrow off first (opticnerve_deploy.py pointer off)")
else:
    host = cfg("ON_HOST")
    password = cfg("ON_VNC_PASSWORD")
    if not host or not password:
        raise Exception("set ON_HOST and ON_VNC_PASSWORD (or ON_LOCAL_SCREEN) before starting the IDE")
    # Offer only VNC authentication (ON-VNC-1)
    Class.forName("com.tigervnc.rfb.SecurityClient").getField("secTypes").get(None).setParam("VncAuth,None")
    vnc = VNCScreen.start(host, int(cfg("ON_VNC_PORT", "5901")), password, 10, 1000)
    scr = vnc

# Positions on the panel, from the panel's top left corner. The panel's corner is found from the RESET button (at
# 750,700 on the panel), since a viewer may not show the panel exactly at its monitor's corner.
ox, oy = 0, 0
def area(x, y, w, h):
    return Region(ox + x, oy + y, w, h) if vnc is None else vnc.newRegion(x, y, w, h)

# A hand on the mouse and keyboard: the one way this test sends input (the model of host/tools/hand.py; the same
# block in each OculiX script, kept identical by tests/regression/test_hand.py). The pointer glides from where it is
# along a slightly curved path, easing in and out, at about 60 pointer events a second (0.5 to 0.8 s by distance,
# about 1.5 px of tremor), rests 80 to 200 ms, then clicks with the button held 50 to 90 ms: OculiX's click, with
# MoveMouseDelay 0 so that OculiX adds no move of its own, and ClickDelay set each time (OculiX resets it after each
# click; with 0 the press and the release go out together, and some applications miss such clicks). Each key is held
# 40 to 80 ms and comes 60 to 180 ms after the last. The agent does not report where the pointer is, so the first
# move starts at the bottom centre of the screen. With glides False (a script's choice), the pointer jumps to the
# target in one move and clicks without the rest; keys keep their pace.
import math, random, time
hand = [None, None]
glides = True
Settings.MoveMouseDelay = 0

def glide(x, y):
    Settings.MoveMouseDelay = 0
    left, top, w, h = scr.getX(), scr.getY(), scr.getW(), scr.getH()
    x, y = min(max(x, left), left + w - 1), min(max(y, top), top + h - 1)
    if not glides:
        scr.getRobot().mouseMove(x, y)
        hand[0], hand[1] = x, y
        return
    x0, y0 = (hand[0], hand[1]) if hand[0] is not None else (left + w // 2, top + h - 1)
    dist = math.hypot(x - x0, y - y0)
    steps, dur, cx, cy = 1, 0.0, x, y
    if dist >= 1:
        dur = 0.5 + 0.3 * min(1.0, dist / 1500.0)
        bend = random.uniform(-0.15, 0.15) * dist
        cx = (x0 + x) / 2.0 - (y - y0) / dist * bend
        cy = (y0 + y) / 2.0 + (x - x0) / dist * bend
        steps = max(2, int(dur * 60))
    robot = scr.getRobot()
    for i in range(1, steps + 1):
        t = float(i) / steps
        e = t * t * (3 - 2 * t)
        qx = (1 - e) ** 2 * x0 + 2 * (1 - e) * e * cx + e * e * x
        qy = (1 - e) ** 2 * y0 + 2 * (1 - e) * e * cy + e * e * y
        if i < steps:
            qx += random.uniform(-1.5, 1.5)
            qy += random.uniform(-1.5, 1.5)
        robot.mouseMove(int(round(min(max(qx, left), left + w - 1))), int(round(min(max(qy, top), top + h - 1))))
        if dur:
            time.sleep(dur / steps)
    hand[0], hand[1] = x, y

def hand_click(x, y):
    glide(x, y)
    if glides:
        time.sleep(random.uniform(0.08, 0.2))
    Settings.ClickDelay = random.uniform(0.05, 0.09)
    scr.click(scr.newLocation(hand[0], hand[1]) if vnc else Location(hand[0], hand[1]))

# Something found by image: a hand aims at it, not at its exact centre (within a quarter of its size).
def click_on(image):
    m = scr.wait(image, 3)
    c = m.getTarget()
    hand_click(c.x + int(random.uniform(-0.25, 0.25) * m.w), c.y + int(random.uniform(-0.25, 0.25) * m.h))

def hand_type(text):
    for ch in text:
        time.sleep(random.uniform(0.06, 0.18))
        scr.keyDown(ch)
        try:
            time.sleep(random.uniform(0.04, 0.08))
        finally:
            scr.keyUp(ch)
# (end of the hand)
glides = cfg("ON_HAND") == "1"

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

# Over VNC, the demo panel must be on the screen before anything is searched (#505: a run began on an all-black
# frame): 1920 x 1080, the title bar's colour at (1500, 30), the lamp red or green. Up to 30 s.
def not_panel(img):
    if img.getWidth() != 1920 or img.getHeight() != 1080:
        return "the frame is %dx%d, not 1920x1080" % (img.getWidth(), img.getHeight())
    t, l = img.getRGB(1500, 30) & 0xffffff, img.getRGB(1190, 370) & 0xffffff
    if t != 0x0a3d62:
        return "the title bar is #%06x, not #0a3d62" % t
    r, g = l >> 16, (l >> 8) & 0xff
    if not (r > 200 and g < 100) and not (g > 130 and r < 100):
        return "the lamp is #%06x, neither red nor green" % l
    return None

try:
    if vnc is not None:
        why, end = "no frame", time.time() + 30
        while time.time() < end:
            why = not_panel(vnc.capture().getImage())
            if why is None:
                break
            sleep(0.25)
        check("the demo panel is on the screen", why is None, why or "1920x1080, title bar and lamp seen")
        if why is not None:  # nothing else is checked on a screen that is not the panel
            print("1 FAILED: the demo panel is on the screen")
            raise SystemExit(1)
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

    hand_type("\r")  # Return clears the text line (Enter as "\r", ON-VNC-1)
    sleep(0.5)
    hand_type("hello pi 5")
    sleep(0.5)
    text = " ".join(area(400, 505, 1360, 80).text().split())
    check("typed text appears on the panel", text == "hello pi 5", 'OCR read "%s"' % text)

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
