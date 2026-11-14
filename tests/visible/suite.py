#!/usr/bin/env python3
"""Visible test suite for the Pi 5 quick-start image: three channels, one recording.

    python tests/visible/suite.py list        the tests in order, their prerequisites and last results
    python tests/visible/suite.py all         record with OBS and run every test, captions on the video
    python tests/visible/suite.py run ID      run (or rerun) one main test: no recording, no setup or teardown
    python tests/visible/suite.py reset       forget all results

Channels:
  SSH     controls the target and reads its state: starts and closes apps, checks processes, core dumps and the
          system log, reboots (host/tools/pi_ssh.sh).
  VNC     is the function under test: input goes in through QNX's vncserv, frames come out of it.
  camera  is the independent witness: a webcam pointed at the HDMI monitor, read through OBS
          (host/tools/obs_control.py), shows what a person would see. OBS also records the run.

The run has three parts. Setup tests (SU..) make the connections and check everything the suite depends on, save the
initial condition and start the recording; if one fails, every main test is skipped with that reason. Then the main
tests. Teardown tests (TD..) always run: they put the target back, stop and rename the recording, restore OBS to
the saved condition and disconnect.

Every test first puts the target into the state it needs over SSH, so a failed test does not leave the next one in
the wrong place. Prerequisites are only functions a test really reuses; if one did not pass, the test is recorded as
SKIP with the reason. Known limits of the image are N/A with the reason. After each test the target's health is
checked: a new core dump or a missing graphics process fails the test and skips the rest. An ID is always shown
with its title.

Environment: ON_HOST (default 10.30.1.116), ON_VNC_PASSWORD and ON_LOGIN_PASSWORD (the image's documented defaults;
not stored here), ON_OCULIX_JAR (the pinned oculixapi jar) and ON_TIGERVNC_JAR (the pinned
tigervnc-java-oculix 2.0.4 jar, first on the classpath: test configuration ON-VNC-1, docs/CONFIG_MANAGEMENT.md), ON_CAMERA (OBS source name,
default "Video Capture Device"), ON_CAMERA_SCENE (default "Scene"). The update tests need /tmp/screen_probe on the
target (agent/spikes/screen_probe.c). Results and frames go to tests/visible/runs/ (not in git).
"""
import base64, hashlib, json, os, re, shutil, subprocess, sys, threading, time
from datetime import datetime, timezone
from pathlib import Path
from PIL import Image, ImageChops, ImageStat

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "host" / "tools"))
import obs_control  # noqa: E402

RUNS = ROOT / "tests" / "visible" / "runs"
STATE = RUNS / "current.json"
HOST = os.environ.get("ON_HOST", "10.30.1.116")
CAMERA = os.environ.get("ON_CAMERA", "Video Capture Device")
CAMERA_SCENE = os.environ.get("ON_CAMERA_SCENE", "Scene")
CAPTION = "OpticNerve caption"
EXPECTED_MOTD = ROOT / "tests" / "expected" / "rpi5-quickstart-20260902" / "motd.txt"

# Positions on the 1920 x 1080 welcome launcher of the quick-start image 20260902, and the terminal's text cells.
TILE = {"terminal": (1107, 460), "gtk4": (794, 768), "gears": (958, 768), "maze": (1124, 768)}
LOGIN_Y, CELL_W = 32, 15
APPS = ["st", "gles3-gears", "gles2-maze", "vkcubepp", "camera_example3_viewfinder", "gtk4-demo"]
GRAPHICS = ["drm-rpi5", "screen", "rpi_thermal", "mtouch", "vncserv", "fullscreen-winmgr", "demolauncher"]
LOG_NOISE = re.compile(r"sshd|sudo|dhcpcd|io_usb|iousb")
LOG_TROUBLE = re.compile(r"error|fail|fault|sigsegv|terminat|crash|abort|core|dump|lost|closed", re.I)


def need(var):
    if not os.environ.get(var):
        sys.exit(f"set {var}")
    return os.environ[var]


# --- SSH: control and state -------------------------------------------------------------------------------------

def pi(cmd, timeout=90):
    """Run CMD on the target; inside it, "rs X" runs X as root. Returns stdout, or None if SSH failed."""
    need("ON_LOGIN_PASSWORD")
    try:
        r = subprocess.run([shutil.which("bash") or "bash", str(ROOT / "host/tools/pi_ssh.sh"), cmd],
                           capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return None
    return r.stdout if r.returncode == 0 else None


def processes():
    out = pi("rs pidin ar") or ""
    return [l.split(None, 1)[1].strip() for l in out.splitlines() if re.match(r"\s*\d+ \S", l)]


def running(name, procs=None):
    procs = processes() if procs is None else procs
    return any(re.match(rf"([^ ]*/)?{re.escape(name)}( |$)", p) for p in procs)


def start(cmd, settle=4):
    """Start a Screen application on the target over SSH, detached."""
    pi(f"nohup {cmd} >/dev/null 2>&1 </dev/null & sleep {settle}")


def close(*names):
    pi("; ".join(f"rs slay -f {n} 2>/dev/null" for n in names) + "; sleep 2")


def to_launcher():
    """Close every app we know of, so the welcome launcher is on screen."""
    procs = processes()
    up = [a for a in APPS if running(a, procs)]
    if up:
        close(*up)
    return up


# --- VNC: the function under test -------------------------------------------------------------------------------

def vnc(*actions):
    subprocess.run([sys.executable, str(ROOT / "host/tools/vnc_input.py"), HOST, need("ON_VNC_PASSWORD"), *actions],
                   check=True, capture_output=True)


def frame(name):
    out = RUNS / f"{name}_vnc.png"
    subprocess.run([sys.executable, str(ROOT / "host/tools/vnc_snapshot.py"), HOST, need("ON_VNC_PASSWORD"), str(out)],
                   check=True, capture_output=True)
    return Image.open(out).convert("RGB")


# --- camera: the witness ----------------------------------------------------------------------------------------

_obs = None


def obs():
    """One WebSocket connection to OBS for the whole run."""
    global _obs
    if _obs is None:
        _obs = obs_control.Obs()
    return _obs


def obs_request(kind, data=None):
    """A request on the shared connection; if OBS dropped it, connect again once."""
    global _obs
    try:
        return obs().request(kind, data)
    except (OSError, EOFError):
        _obs = None
        return obs().request(kind, data)


def cam(name):
    """One frame of the monitor as the webcam sees it, through OBS."""
    out = RUNS / f"{name}_cam.png"
    r = obs_request("GetSourceScreenshot", {"sourceName": CAMERA, "imageFormat": "png"})
    out.write_bytes(base64.b64decode(r["imageData"].split(",", 1)[1]))
    return Image.open(out).convert("RGB")


def caption(text):
    """Text burnt into the recording, top centre of the camera scene."""
    class o:  # every caption request goes over the shared connection
        request = staticmethod(obs_request)
    names = [i["inputName"] for i in o.request("GetInputList")["inputs"]]
    settings = {"text": text, "font": {"face": "Segoe UI", "size": 56, "style": "Bold"},
                "bk_color": 0, "bk_opacity": 75, "outline": False}
    if CAPTION not in names:
        o.request("CreateInput", {"sceneName": CAMERA_SCENE, "inputName": CAPTION, "inputKind": "text_gdiplus_v3",
                                  "inputSettings": settings, "sceneItemEnabled": True})
    else:
        o.request("SetInputSettings", {"inputName": CAPTION, "inputSettings": settings})
    item = o.request("GetSceneItemId", {"sceneName": CAMERA_SCENE, "sourceName": CAPTION})["sceneItemId"]
    o.request("SetSceneItemTransform", {"sceneName": CAMERA_SCENE, "sceneItemId": item, "sceneItemTransform": {
        "positionX": 960, "positionY": 4, "alignment": 4}})  # 4 = top, horizontally centred
    o.request("SetSceneItemEnabled", {"sceneName": CAMERA_SCENE, "sceneItemId": item, "sceneItemEnabled": bool(text)})


# --- comparing frames -------------------------------------------------------------------------------------------

def changed(a, b):
    """Bounding box of the pixels that differ at all (for VNC frames, which are exact), or None."""
    return ImageChops.difference(a, b).getbbox()


def share(a, b, level=24):
    """Share of pixels that differ by more than LEVEL: robust against camera noise (0.003 % on a static screen)."""
    h = ImageChops.difference(a, b).convert("L").histogram()
    return sum(h[level + 1:]) / (a.width * a.height)


def brightness(img):
    return ImageStat.Stat(img.convert("L")).mean[0]


def agree(camera, vncframe):
    """Correlation of the camera's view with the VNC frame, both shrunk to 48 x 27 grey: about 0.9 when the monitor
    shows what vncserv serves (a static screen), about 0 when it shows something else."""
    a = list(camera.convert("L").resize((48, 27)).tobytes())
    b = list(vncframe.convert("L").resize((48, 27)).tobytes())
    ma, mb = sum(a) / len(a), sum(b) / len(b)
    da = sum((x - ma) ** 2 for x in a) ** .5
    db = sum((y - mb) ** 2 for y in b) ** .5
    return sum((x - ma) * (y - mb) for x, y in zip(a, b)) / (da * db) if da and db else 0.0


def live(tag, grab):
    """Share of the picture that changes between two frames a second apart."""
    a = grab(f"{tag}_1")
    time.sleep(1)
    return share(a, grab(f"{tag}_2"))


# Test configuration ON-VNC-1: the newer VNC client library first on the classpath, both jars checked by hash.
PINNED = {"ON_TIGERVNC_JAR": "de6fc7ca87d9fd704b2b1b2d172f19e55c34f53cadd7e664739315ac15224ca6",
          "ON_OCULIX_JAR": {"b8ff76a74e582c7d36df2ca1dfb3f09bf564395d47502a9da5db84a304f70638",   # windows
                            "bfebf7841eeca171e632739c00eeba879683c32d7530e9af624ae447010c9554",   # linux
                            "e26cc3daa7bf064d222a2457801b79f5a22007a84bba6853631edc54f6074bd3"}}  # macos


def jar_ok(var):
    path = Path(os.environ.get(var, "-"))
    if not path.is_file():
        return False
    want = PINNED[var]
    return hashlib.sha256(path.read_bytes()).hexdigest() in ({want} if isinstance(want, str) else want)


def java(*args):
    cp = os.pathsep.join([need("ON_TIGERVNC_JAR"), need("ON_OCULIX_JAR")])
    return subprocess.run(["java", "--enable-native-access=ALL-UNNAMED", "-cp", cp, *args],
                          capture_output=True, text=True, cwd=ROOT, timeout=300)  # a hung VNC handshake fails the test


# --- setup and teardown tests ----------------------------------------------------------------------------------

RUN = {}  # the state of the run in progress (set by run()), where the initial condition is kept


def obs_condition():
    """The parts of OBS's state the suite changes: program scene, the caption source, audio mutes."""
    inputs = [i["inputName"] for i in obs_request("GetInputList")["inputs"]]
    mutes = {}
    for name in inputs:
        try:
            mutes[name] = obs_request("GetInputMute", {"inputName": name})["inputMuted"]
        except RuntimeError:
            pass  # not an audio source
    shown = None
    if CAPTION in inputs:
        item = obs_request("GetSceneItemId", {"sceneName": CAMERA_SCENE, "sourceName": CAPTION})["sceneItemId"]
        shown = obs_request("GetSceneItemEnabled", {"sceneName": CAMERA_SCENE,
                                                    "sceneItemId": item})["sceneItemEnabled"]
    return {"scene": obs_request("GetCurrentProgramScene")["currentProgramSceneName"],
            "caption_present": CAPTION in inputs, "caption_shown": shown, "mutes": mutes,
            "recording": obs_request("GetRecordStatus")["outputActive"]}


def t_su_obs():
    v = obs().request("GetVersion")  # opens the one connection of the run
    scenes = [x["sceneName"] for x in obs_request("GetSceneList")["scenes"]]
    inputs = [i["inputName"] for i in obs_request("GetInputList")["inputs"]]
    RUN["initial_obs"] = c = obs_condition()
    ok = CAMERA_SCENE in scenes and CAMERA in inputs and not c["recording"]
    return ("PASS" if ok else "FAIL"), (
        f"OBS {v['obsVersion']}, obs-websocket {v['obsWebSocketVersion']}: connected; scene '{CAMERA_SCENE}' "
        f"present {CAMERA_SCENE in scenes}; camera source '{CAMERA}' present {CAMERA in inputs}; not already "
        f"recording {not c['recording']}; initial condition saved: scene '{c['scene']}', caption source present "
        f"{c['caption_present']}, muted {c['mutes']}")


def t_su_host():
    checks = {
        "java": shutil.which("java") is not None,
        "OculiX jar with its pinned hash": jar_ok("ON_OCULIX_JAR"),
        "tigervnc-java-oculix 2.0.4 jar with its pinned hash (ON-VNC-1)": jar_ok("ON_TIGERVNC_JAR"),
        "expected welcome text for this release": EXPECTED_MOTD.is_file(),
        "VNC and login passwords set": bool(os.environ.get("ON_VNC_PASSWORD") and os.environ.get("ON_LOGIN_PASSWORD")),
        "no unnamed recording waiting to be overwritten": obs_control.recording_blocker(obs()) is None,
    }
    bad = [k for k, v in checks.items() if not v]
    return ("PASS", "present: " + ", ".join(checks)) if not bad else ("FAIL", "missing: " + ", ".join(bad))


def t_su_record():
    obs_request("SetCurrentProgramScene", {"sceneName": CAMERA_SCENE})
    obs_control.start_record(obs())
    time.sleep(2)
    c = cam("setup")
    active = obs_request("GetRecordStatus")["outputActive"]
    ok = active and brightness(c) > 5
    return ("PASS" if ok else "FAIL"), (f"OBS on scene '{CAMERA_SCENE}', recording {active}; camera frame "
                                        f"{c.width}x{c.height}, brightness {brightness(c):.0f}/255")


def t_su_ssh():
    out = (pi("uname -sr; hostname; date -u '+%Y-%m-%d %H:%M UTC'; rs id -u", timeout=30) or "").splitlines()
    out = [l.strip() for l in out if l.strip()]
    ok = len(out) == 4 and out[-1] == "0"
    return ("PASS" if ok else "FAIL"), (f"SSH to {HOST}: {', '.join(out[:3]) or 'no answer'}; root through sudo "
                                        f"{bool(out) and out[-1] == '0'}")


def t_su_target():
    procs = processes()
    apps = [a for a in APPS if running(a, procs)]
    down = [g for g in GRAPHICS if not running(g, procs)]
    probe = "yes" in (pi("test -x /tmp/screen_probe && echo yes") or "")
    v = frame("initial")
    RUN["initial_target"] = {"apps": apps, "cores": RUN.get("baseline")}
    ok = not apps and not down and probe and v.size == (1920, 1080)
    return ("PASS" if ok else "FAIL"), (
        f"SSH: graphics processes all up {not down}{' (down: ' + ', '.join(down) + ')' if down else ''}; no app "
        f"open on the launcher {not apps}{' (open: ' + ', '.join(apps) + ')' if apps else ''}; /tmp/screen_probe "
        f"present {probe}; initial condition saved: launcher frame, core dumps {RUN.get('baseline')}")


def t_td_target():
    closed = to_launcher()
    out = pi("rs ls /data/var/dumper") or ""
    procs = processes()
    apps = [a for a in APPS if running(a, procs)]
    down = [g for g in GRAPHICS if not running(g, procs)]
    new = sorted(set(out.split()) - set(RUN.get("baseline") or []))
    v = frame("final")
    diff = share(Image.open(RUNS / "initial_vnc.png").convert("RGB"), v)
    k = agree(cam("final"), v) if _obs is not None else None  # no camera if OBS never connected
    ok = not apps and not down and not new and diff < 0.01 and (k is None or k > 0.6)
    return ("PASS" if ok else "FAIL"), (
        f"SSH: apps closed now {closed or 'none'}, still open {apps or 'none'}, graphics processes down "
        f"{down or 'none'}, new core dumps {new or 'none'}; VNC frame differs from the initial launcher frame by "
        f"{diff:.2%}; " + (f"camera sees the launcher (correlation {k:.2f})" if k is not None
                      else "camera not checked: OBS was never connected"))


def t_td_record():
    target = obs_control.stop_record(obs(), "visible-suite")
    RUN["recording"] = str(target)
    ok = target.is_file() and target.stat().st_size > 0
    return ("PASS" if ok else "FAIL"), f"recording stopped and renamed: {target} ({target.stat().st_size / 1e6:.1f} MB)"


def t_td_obs():
    global _obs
    want = RUN["initial_obs"]
    if want["caption_present"]:
        caption("")
        item = obs_request("GetSceneItemId", {"sceneName": CAMERA_SCENE, "sourceName": CAPTION})["sceneItemId"]
        obs_request("SetSceneItemEnabled", {"sceneName": CAMERA_SCENE, "sceneItemId": item,
                                            "sceneItemEnabled": bool(want["caption_shown"])})
    elif obs_condition()["caption_present"]:
        obs_request("RemoveInput", {"inputName": CAPTION})
    for name, muted in want["mutes"].items():
        obs_request("SetInputMute", {"inputName": name, "inputMuted": muted})
    obs_request("SetCurrentProgramScene", {"sceneName": want["scene"]})
    time.sleep(0.5)
    now = obs_condition()
    differs = [k for k in want if now[k] != want[k]]
    obs().close()
    _obs = None
    return ("PASS" if not differs else "FAIL"), (
        f"OBS back to the saved condition: scene '{now['scene']}', caption source present {now['caption_present']}, "
        f"muted {now['mutes']}, recording {now['recording']}" +
        (f"; NOT restored: {differs}" if differs else "") + "; WebSocket closed")


# --- the main tests: each returns (result, detail) ---------------------------------------------------------------

def t_vnc_frame():
    to_launcher()
    f = frame("launcher")
    return ("PASS", f"VNC: full frame {f.width}x{f.height}") if f.size == (1920, 1080) else ("FAIL", f"VNC: {f.size}")


def t_camera_sees_monitor():
    to_launcher()
    v, c = frame("launcher"), cam("launcher")
    b, k = brightness(c), agree(c, v)
    ok = b > 25 and k > 0.6
    return ("PASS" if ok else "FAIL"), (f"camera: {c.width}x{c.height}, brightness {b:.0f}/255, agrees with the VNC "
                                         f"frame of the launcher (correlation {k:.2f}, needs > 0.6)")


def t_pointer_square():
    to_launcher()
    pi("rs /tmp/screen_probe status >/dev/null")  # reading the counters resets them
    corners = [(560, 240), (1360, 240), (1360, 840), (560, 840), (560, 240)]
    actions = []
    for (x0, y0), (x1, y1) in zip(corners, corners[1:]):
        for i in range(1, 11):
            actions += [f"move:{x0 + (x1 - x0) * i // 10},{y0 + (y1 - y0) * i // 10}", "wait:0.1"]
    vnc(*actions, "move:960,540")
    out = pi("rs /tmp/screen_probe status") or ""
    m = re.search(r"metrics: .*?event=(\d+)", out)
    events = int(m.group(1)) if m else -1
    return ("PASS" if events >= 41 else "FAIL"), (f"VNC: 41 pointer moves sent round a square; SSH: Screen counted "
                                                  f"{events} input events")


def open_terminal():
    """The terminal has to be opened from its tile: started over SSH its login prompt takes no password."""
    to_launcher()
    vnc(f"click:{TILE['terminal'][0]},{TILE['terminal'][1]}", "wait:3")
    return running("st")


def t_click_launch():
    to_launcher()
    v0, c0 = frame("click_before"), cam("click_before")
    vnc(f"click:{TILE['terminal'][0]},{TILE['terminal'][1]}", "wait:3")
    st = running("st")
    sv, sc = share(v0, frame("click_after")), share(c0, cam("click_after"))
    close("st")
    ok = st and sv > 0.5 and sc > 0.3
    return ("PASS" if ok else "FAIL"), (f"VNC click on the Terminal tile; SSH: st running {st}; VNC frame changed "
                                        f"{sv:.0%}; camera: monitor changed {sc:.0%}")


def t_keys():
    if not open_terminal():
        return "FAIL", "the terminal did not open"
    v0, c0 = frame("keys_before"), cam("keys_before")
    vnc("text:on", "wait:1")
    v1, c1 = frame("keys_after"), cam("keys_after")
    box = changed(v0, v1)
    seen = int(share(c0, c1) * c0.width * c0.height)
    close("st")
    ok = box and box[2] - box[0] <= 2 * CELL_W + 2 and box[1] < LOGIN_Y < box[3]
    return ("PASS" if ok else "FAIL"), (f"VNC: 'on' typed, frame changed only in {box}; camera: {seen} pixels "
                                        f"changed on the monitor")


def t_backspace():
    if not open_terminal():
        return "FAIL", "the terminal did not open"
    v0 = frame("backspace_before")
    vnc("text:on", "wait:0.5", "key:0xff08", "key:0xff08", "wait:1")
    box = changed(v0, frame("backspace_after"))
    close("st")
    return ("PASS", "VNC: 'on' typed and erased, frame identical to the one before") if box is None \
        else ("FAIL", f"VNC: frame still differs in {box}")


def t_login():
    if not open_terminal():
        return "FAIL", "the terminal did not open"
    v0 = frame("login_before")
    vnc("text:qnxuser", "key:0xff0d", "wait:1.5", "text:" + need("ON_LOGIN_PASSWORD"), "key:0xff0d", "wait:3")
    v1, c1 = frame("login_after"), cam("login_after")
    getty = running("getty")
    sv = share(v0, v1)
    ok = sv > 0.02 and not getty
    return ("PASS" if ok else "FAIL"), (f"VNC: user and password typed, {sv:.0%} of the frame changed; SSH: login "
                                        f"prompt (getty) gone {not getty}; camera agrees with VNC "
                                        f"{agree(c1, v1):.2f}")


def t_capture():
    procs = processes()
    if not running("st", procs) or running("getty", procs):
        return "FAIL", "no logged-in terminal on screen"
    png, txt = RUNS / "motd_oculix.png", RUNS / "motd_ocr.txt"
    r = java("host/spikes/ocr/OcrScreen.java", HOST, need("ON_VNC_PASSWORD"), str(png), str(txt))
    if r.returncode or not png.exists():
        return "FAIL", str((r.stdout + r.stderr).strip().splitlines()[-1:])
    return "PASS", "OculiX over VNC: captured 1920x1080; " + \
        next((l for l in r.stdout.splitlines() if l.startswith("OCR:")), "")


def t_ocr_motd():
    r = subprocess.run([sys.executable, str(ROOT / "host/tools/ocr_compare.py"), str(EXPECTED_MOTD),
                        str(RUNS / "motd_ocr.txt")], capture_output=True, text=True)
    (RUNS / "motd_compare.txt").write_text(r.stdout)
    missing = [l[10:] for l in r.stdout.splitlines() if l.startswith("MISS")]
    summary = r.stdout.strip().splitlines()[-1] if r.stdout.strip() else r.stderr.strip()
    close("st")
    return ("PASS", summary) if r.returncode == 0 else ("FAIL", f"{summary}; missing: {missing}")


def app_shows(tag, proc):
    """Common check for a graphics app: process up (SSH), moving picture in the VNC frames and on the monitor."""
    up = running(proc)
    lv, lc = live(tag, frame), live(tag, cam)
    ok = up and lv > 0.01 and lc > 0.01
    return ok, (f"SSH: {proc} running {up}; VNC frames a second apart differ {lv:.0%}; camera: the monitor's "
                f"picture moves ({lc:.0%})")


def back_to_launcher(tag):
    c = cam(f"{tag}_closed")
    k = agree(c, Image.open(RUNS / "launcher_vnc.png"))
    return k > 0.6, f"closed over SSH, camera sees the launcher again (correlation {k:.2f})"


def tile_app(name, proc):
    to_launcher()
    vnc(f"click:{TILE[name][0]},{TILE[name][1]}", "wait:4")
    ok, detail = app_shows(name, proc)
    close(proc)
    back, d2 = back_to_launcher(name)
    return ("PASS" if ok and back else "FAIL"), f"VNC click on the tile; {detail}; {d2}"


def t_gears():
    return tile_app("gears", "gles3-gears")


def t_maze():
    return tile_app("maze", "gles2-maze")


def t_gtk4():
    """The tile with the cube icon is the GTK4 demo (gtk4.desktop), not vkcubepp. Its window is static, so the
    check is that the monitor shows the same new picture as the VNC frame."""
    to_launcher()
    v0, c0 = frame("gtk4_before"), cam("gtk4_before")
    vnc(f"click:{TILE['gtk4'][0]},{TILE['gtk4'][1]}", "wait:6")
    up = running("gtk4-demo")
    v1, c1 = frame("gtk4_open"), cam("gtk4_open")
    sv, sc, k = share(v0, v1), share(c0, c1), agree(c1, v1)
    close("gtk4-demo")
    back, d2 = back_to_launcher("gtk4")
    ok = up and sv > 0.5 and sc > 0.3 and back
    return ("PASS" if ok else "FAIL"), (f"VNC click on the tile; SSH: gtk4-demo running {up}; VNC frame changed "
                                        f"{sv:.0%}; camera: monitor changed {sc:.0%} (correlation with the VNC "
                                        f"frame {k:.2f}: weak on a nearly blank window, so not used here); {d2}")


def t_viewfinder():
    to_launcher()
    start("camera_example3_viewfinder", 5)
    ok, detail = app_shows("viewfinder", "camera_example3_viewfinder")
    close("camera_example3_viewfinder")
    back, d2 = back_to_launcher("viewfinder")
    return ("PASS" if ok and back else "FAIL"), f"started over SSH; {detail}; {d2}"


def t_alt_del():
    to_launcher()
    start("/usr/bin/gles3-gears")
    if not running("gles3-gears"):
        return "FAIL", "gears did not start over SSH"
    vnc("combo:0xffe9+0xffff", "wait:3")
    still = running("gles3-gears")
    close("gles3-gears")
    if still:
        return "FAIL", ("Alt+Del sent over VNC (Alt_L down, Delete down, up, up); SSH: gears still running. The "
                        "launcher's footer says Alt+Del closes windows; not checked with a real keyboard")
    return "PASS", "Alt+Del sent over VNC; SSH: gears gone"


def watch(seconds, moves):
    t = threading.Thread(target=vnc, args=tuple(moves)) if moves else None
    if t:
        t.start()
    out = pi(f"rs /tmp/screen_probe watch {seconds}") or ""
    if t:
        t.join()
    m = re.search(r"total updates (\d+)", out)
    counts = re.findall(r"metrics: .*? update=(\d+)", out)
    return (int(m.group(1)) if m else -1), (int(counts[-1]) if counts else -1)


def t_update_on_change():
    to_launcher()
    pulses, updates = watch(8, ["wait:2", "move:300,300", "move:600,300", "move:900,500", "move:1200,600",
                                "move:1500,800"])
    ok = pulses > 0 and updates > 0
    return ("PASS" if ok else "FAIL"), (f"5 pointer moves over VNC; SSH: {pulses} update pulses, Screen counted "
                                        f"{updates} updates (the pulse says 'changed'; it is not a frame count)")


def t_update_static():
    to_launcher()
    time.sleep(2)
    pulses, updates = watch(6, None)
    return ("PASS" if pulses == 0 else "FAIL"), f"static launcher, 6 s; SSH: {pulses} update pulses, {updates} updates"


def t_oculix_click():
    to_launcher()
    pattern = RUNS / "gears_tile.png"  # cut from the current frame, so no image of QNX's is stored in the repo
    x, y = TILE["gears"]
    frame("oculix_before").crop((x - 58, y - 58, x + 58, y + 58)).save(pattern)
    r = java("host/spikes/s1/S1VncScreen.java", HOST, need("ON_VNC_PASSWORD"), "find", str(pattern), str(RUNS), "click")
    found = [l for l in r.stdout.splitlines() if l.startswith("Finder at similarity 0.99")]
    clicked = any(l.startswith("clicked") for l in r.stdout.splitlines())
    ok, detail = app_shows("oculix_gears", "gles3-gears")
    close("gles3-gears")
    hit = found[0].split(": ", 1)[1] if found else "not found"
    return ("PASS" if found and "score" in found[0] and clicked and ok else "FAIL"), \
        f"OculiX over VNC: Gears tile {hit}, clicked {clicked}; {detail}"


def t_shutdown_to_launcher():
    """The way out of the terminal on this image (David, 2026-11-13): sudo shutdown, which reboots the Pi."""
    if not open_terminal():
        return "FAIL", "the terminal did not open"
    vnc("text:qnxuser", "key:0xff0d", "wait:1.5", "text:" + need("ON_LOGIN_PASSWORD"), "key:0xff0d", "wait:3",
        "text:sudo shutdown", "key:0xff0d", "wait:2", "text:" + need("ON_LOGIN_PASSWORD"), "key:0xff0d")
    launcher = Image.open(RUNS / "launcher_vnc.png").convert("RGB")
    t0, went_down, dark = time.time(), False, False
    while time.time() - t0 < 240:
        time.sleep(4)
        dark = dark or brightness(cam("reboot")) < 12
        if pi("echo up", timeout=15) is None:
            went_down = True
            continue
        if went_down or dark:
            try:
                if share(launcher, frame("reboot")) < 0.01 and agree(cam("reboot"), launcher) > 0.6:
                    return "PASS", (f"'sudo shutdown' typed over VNC; SSH went down and came back: {went_down}; "
                                    f"camera saw the monitor go dark: {dark}; launcher back on VNC and on the "
                                    f"monitor after {time.time() - t0:.0f} s")
            except subprocess.CalledProcessError:
                pass  # vncserv not up yet
    return "FAIL", f"no launcher within 240 s (SSH went down: {went_down}, monitor went dark: {dark})"


MOUSE = ("the quick-start image's terminal is sandboxed and ignores the mouse, even a real one (David, 2026-11-13); "
         "to be tested in an app that takes the mouse")

# (id, title, what the monitor shows, prerequisites, function)
SETUP = [
    ("SU01", "OBS connects, initial condition saved", "nothing", [], t_su_obs),
    ("SU02", "host prerequisites", "nothing", ["SU01"], t_su_host),
    ("SU03", "recording starts on the camera scene", "nothing", ["SU02"], t_su_record),
    ("SU04", "SSH to the target", "nothing", [], t_su_ssh),
    ("SU05", "target prerequisites, initial condition saved", "the welcome launcher", ["SU04"], t_su_target),
]
TEARDOWN = [
    ("TD01", "target back to its initial condition", "the welcome launcher", ["SU05"], t_td_target),
    ("TD02", "recording stopped and renamed", "nothing", ["SU03"], t_td_record),
    ("TD03", "OBS restored and disconnected", "nothing", ["SU01"], t_td_obs),
]
MAIN = [
    ("F01", "VNC connect and full frame", "the welcome launcher", [], t_vnc_frame),
    ("C01", "camera sees the monitor", "the welcome launcher", ["F01"], t_camera_sees_monitor),
    ("F02", "pointer moves", "the pointer traces a square and stops in the centre", ["F01"], t_pointer_square),
    ("F03", "click opens an app", "the Terminal tile opens the terminal, which is closed again", ["F01", "C01"],
     t_click_launch),
    ("F04", "keys reach the app", "the terminal opens, 'on' is typed after login:", ["F03"], t_keys),
    ("F05", "Backspace", "the terminal opens, 'on' is typed and erased", ["F04"], t_backspace),
    ("F06", "click puts the cursor", "nothing", ["F04"], None),
    ("F07", "drag selects text", "nothing", ["F06"], None),
    ("U01", "unboxing: log in", "the terminal opens, login as qnxuser, the welcome message appears", ["F04"], t_login),
    ("U02", "unboxing: OculiX capture and OCR", "the welcome message stays", ["U01"], t_capture),
    ("U03", "unboxing: welcome text is right for this release", "the terminal closes", ["U02"], t_ocr_motd),
    ("A04", "Gears tile", "gears turn, then the launcher", ["F03"], t_gears),
    ("A05", "Maze tile", "the brick maze, then the launcher", ["F03"], t_maze),
    ("A06", "GTK4 demo tile (cube icon)", "the GTK4 demo window, then the launcher", ["F03"], t_gtk4),
    ("A01", "camera viewfinder runs", "moving colour bars, then the launcher", ["C01"], t_viewfinder),
    ("A02", "Alt+Del closes an app", "gears, Alt+Del, then the launcher", ["F04"], t_alt_del),
    ("S01", "update signal fires on a change", "the pointer jumps to 5 spots", ["F02"], t_update_on_change),
    ("S02", "no update signal on a static screen", "the launcher, still", [], t_update_static),
    ("O01", "OculiX finds the Gears tile and clicks it", "gears open, then the launcher", ["F03"], t_oculix_click),
    ("A03", "sudo shutdown returns to the launcher", "login, 'sudo shutdown', the Pi reboots to the launcher",
     ["U01"], t_shutdown_to_launcher),
]
TESTS = SETUP + MAIN + TEARDOWN
BY_ID = {t[0]: t for t in TESTS}
GATE = [t[0] for t in SETUP]  # every main test needs all of setup
HOST_ONLY = {"SU01", "SU02", "SU03", "TD02", "TD03"}  # no target health check after these
KNOWN_LIMITS = {"F06": MOUSE, "F07": MOUSE}


def label(tid):
    """ID and title together: the ID is the key, the title is what a person follows."""
    return f"{tid} ({BY_ID[tid][1]})"


def load():
    return json.loads(STATE.read_text()) if STATE.exists() else {"started": None, "results": {}, "baseline": None}


def save(state):
    RUNS.mkdir(parents=True, exist_ok=True)
    STATE.write_text(json.dumps(state, indent=1))


def health(state):
    """New core dumps, graphics processes that are gone, and new system log lines that look like trouble."""
    out = pi("rs ls /data/var/dumper; echo ---; rs pidin ar; echo ---; rs slog2info | tail -n 800")
    if out is None or out.count("---\n") < 2:
        return ["no SSH access"], [], []
    cores, procs, log = (part.strip().splitlines() for part in out.split("---\n", 2))
    procs = [l.split(None, 1)[1].strip() for l in procs if re.match(r"\s*\d+ \S", l)]
    down = [p for p in GRAPHICS if not running(p, procs)]
    mark = state.get("log_mark")
    new = log[log.index(mark) + 1:] if mark in log else log[-100:] if mark else []
    state["log_mark"] = log[-1] if log else mark
    flagged = [l for l in new if LOG_TROUBLE.search(l) and not LOG_NOISE.search(l)]
    if state["baseline"] is None:
        state["baseline"] = cores
    return sorted(set(cores) - set(state["baseline"])), down, flagged


def run(tid, state, gate=True):
    global RUN
    RUN = state
    _, _, _, requires, fn = BY_ID[tid]
    results = state["results"]
    main = tid not in GATE and not tid.startswith("TD")
    if main and gate:
        requires = GATE + requires
    unhealthy = next((r for r in results.values() if r.get("unhealthy") and r["id"] != tid), None)
    if tid in KNOWN_LIMITS:
        result, detail = "N/A", "known limitation: " + KNOWN_LIMITS[tid]
    elif unhealthy and main:
        result, detail = "SKIP", f"target unhealthy since {label(unhealthy['id'])}: {unhealthy['unhealthy']}"
    else:
        blocking = [(p, results.get(p, {}).get("result", "not run")) for p in requires
                    if results.get(p, {}).get("result") != "PASS"]
        if blocking:
            result, detail = "SKIP", "prerequisite " + ", ".join(f"{label(p)} {r}" for p, r in blocking)
        else:
            t0 = time.time()
            try:
                result, detail = fn()
            except Exception as e:  # a broken test is a failed test, never a silent pass
                result, detail = "FAIL", f"error: {e!r}"
            detail = f"{detail} [{time.time() - t0:.0f} s]"
    entry = {"id": tid, "result": result, "detail": str(detail), "at": datetime.now(timezone.utc).isoformat()}
    if result not in ("SKIP", "N/A") and tid not in HOST_ONLY:
        new_cores, down, flagged = health(state)
        if flagged:
            with open(RUNS / "log_flags.txt", "a", encoding="utf-8") as f:
                f.writelines(f"{tid}: {l}\n" for l in flagged)
            entry["detail"] += f"; target log: {len(flagged)} new line(s) flagged, see runs/log_flags.txt"
        if new_cores or down:
            entry["unhealthy"] = f"new core dumps {new_cores}, processes down {down}"
            entry["result"] = "FAIL"
    results[tid] = entry
    save(state)
    print(f"{label(tid)}: {entry['result']} - {entry['detail']}" +
          (f" - TARGET UNHEALTHY: {entry['unhealthy']}" if "unhealthy" in entry else ""), flush=True)
    return entry


def fresh():
    state = {"started": datetime.now(timezone.utc).isoformat(), "results": {}, "baseline": None, "log_mark": None}
    RUNS.mkdir(parents=True, exist_ok=True)
    (RUNS / "log_flags.txt").unlink(missing_ok=True)
    health(state)
    save(state)
    return state


def run_all():
    """Setup tests, main tests, teardown tests; a caption per test on the recording."""
    state = fresh()
    tally = {}

    def show(text):
        if _obs is not None:  # captions only while the run's connection is open
            try:
                caption(text)
            except (RuntimeError, OSError, EOFError):
                pass

    for n, (tid, title, _, _, _) in enumerate(TESTS, 1):
        if tid == "TD02":  # last thing on the video: the tally so far
            show(f"{n - 1} tests so far: " + ", ".join(f"{v} {k}" for k, v in sorted(tally.items())))
            time.sleep(5)
        show(f"{n}/{len(TESTS)}  {tid} ({title}): running")
        entry = run(tid, state)
        tally[entry["result"]] = tally.get(entry["result"], 0) + 1
        show(f"{n}/{len(TESTS)}  {tid} ({title}): {entry['result']}")
        time.sleep(2.5 if entry["result"] in ("PASS", "FAIL") else 1.5)
    print(f"{len(TESTS)} tests: " + ", ".join(f"{v} {k}" for k, v in sorted(tally.items())))


def main():
    cmd = sys.argv[1] if len(sys.argv) > 1 else "list"
    state = load()
    if cmd == "reset":
        fresh()
        print("new run")
    elif cmd == "list":
        for n, (tid, _, sees, requires, _) in enumerate(TESTS, 1):
            r = state["results"].get(tid, {})
            print(f"{n:2}/{len(TESTS)}  {label(tid)}: {r.get('result') or 'not run'}")
            print(f"        monitor: {sees}")
            print(f"        needs:   {'; '.join(label(p) for p in requires) or 'nothing'}")
    elif cmd == "all":
        run_all()
    elif cmd == "run":
        if state["started"] is None:
            state = fresh()
        run(sys.argv[2], state, gate=False)
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
