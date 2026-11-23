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
