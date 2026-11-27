# A new OculiX IDE test for the Pi's screen. Copy this folder, then rename the folder and this file to the test's
# name. Needs ON_HOST and ON_VNC_PASSWORD (and ON_VNC_PORT, default 5901) in the environment the IDE was started
# from or in the settings file ~/.config/opticnerve/env (see ../README.md).
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

host = cfg("ON_HOST")
password = cfg("ON_VNC_PASSWORD")
if not host or not password:
    raise Exception("set ON_HOST and ON_VNC_PASSWORD before starting the IDE")
# Offer only VNC authentication (configuration ON-VNC-1)
Class.forName("com.tigervnc.rfb.SecurityClient").getField("secTypes").get(None).setParam("VncAuth,None")
vnc = VNCScreen.start(host, int(cfg("ON_VNC_PORT", "5901")), password, 10, 1000)
scr = vnc

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
    #   click_on(tile)            a click by hand; hand_click(x, y) on a point, hand_type("text") on the keyboard
    pass

    # Leave the screen as you found it: undo what the test did, then check the start state again.
finally:
    vnc.stop()

print("ALL PASSED" if not failed else "%d FAILED: %s" % (len(failed), ", ".join(failed)))
