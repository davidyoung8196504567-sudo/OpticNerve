#!/usr/bin/env python3
"""Visible test suite for the Pi 5 quick-start image, run one test at a time over VNC.

    python tests/visible/suite.py list             the tests in order, their prerequisites and last results
    python tests/visible/suite.py next             run the next test that has no result yet
    python tests/visible/suite.py run ID           run (or rerun) one test
    python tests/visible/suite.py confirm ID pass|fail [NOTE...]
                                                   record what a person saw, for tests that need an observer
    python tests/visible/suite.py reset            forget all results and start a new run

Foundation functions are tested before the functions that reuse them. Every test lists its prerequisites; a test
whose prerequisite did not pass is not run but recorded as SKIP, with the reason naming the prerequisite and its
result. A test that leaves the target unhealthy (a new core dump in /data/var/dumper, or a graphics process gone)
fails, and every later test is skipped.

Environment: ON_HOST (default 10.30.1.116), ON_VNC_PASSWORD and ON_LOGIN_PASSWORD (the image's documented defaults;
not stored here), ON_PI_SSH: a shell script that runs its argument as a command on the Pi, where "rs CMD" runs CMD
as root (without it the target health checks and the update-count tests are skipped), ON_OCULIX_JAR: the pinned
oculixapi jar (docs/CONFIG_MANAGEMENT.md). Results go to tests/visible/runs/current.json (not in git).
"""
import json, os, subprocess, sys, threading, time
from datetime import datetime, timezone
from pathlib import Path
from PIL import Image, ImageChops

ROOT = Path(__file__).resolve().parents[2]
RUNS = ROOT / "tests" / "visible" / "runs"
STATE = RUNS / "current.json"
HOST = os.environ.get("ON_HOST", "10.30.1.116")
EXPECTED_MOTD = ROOT / "tests" / "expected" / "rpi5-quickstart-20260902" / "motd.txt"

# Positions on the 1920 x 1080 welcome screen of the quick-start image 20260902, and the terminal's text cells.
TILE = {"terminal": (1107, 460), "cube": (794, 768), "gears": (958, 768), "maze": (1124, 768)}
LOGIN_X, CELL_W, LOGIN_Y = 72, 15, 32  # first character after "login: ", cell width, middle of that text row
ALT_DEL = "combo:0xffe9+0xffff"
GRAPHICS = ["drm-rpi5", "screen", "rpi_thermal", "mtouch", "vncserv", "fullscreen-winmgr", "demolauncher"]


def need(var):
    v = os.environ.get(var)
    if not v:
        sys.exit(f"set {var}")
    return v


def vnc(*actions):
    subprocess.run([sys.executable, str(ROOT / "host/tools/vnc_input.py"), HOST, need("ON_VNC_PASSWORD"), *actions],
                   check=True, capture_output=True)


def frame(name):
    out = RUNS / f"{name}.png"
    subprocess.run([sys.executable, str(ROOT / "host/tools/vnc_snapshot.py"), HOST, need("ON_VNC_PASSWORD"), str(out)],
                   check=True, capture_output=True)
    return Image.open(out).convert("RGB")


def changed(a, b):
    """Bounding box of the pixels that differ, or None."""
    return ImageChops.difference(a, b).getbbox()


def share_changed(a, b):
    d = ImageChops.difference(a, b).convert("L").point(lambda v: 255 if v > 24 else 0)
    return sum(d.histogram()[255:]) / (a.width * a.height)


def pi(cmd):
    script = os.environ.get("ON_PI_SSH")
    if not script:
        return None
    r = subprocess.run(["bash", script, cmd], capture_output=True, text=True)
    return r.stdout


def running(name):
    """True if a process whose executable is NAME (or ends in /NAME) runs; None without SSH access."""
    out = pi(f"rs pidin ar | grep -c -E '^ *[0-9]+ ([^ ]*/)?{name}( |$)'")
    return None if out is None else int(out.strip() or 0) > 0


def java(*args):
    jar = need("ON_OCULIX_JAR")
    return subprocess.run(["java", "-cp", jar, *args], capture_output=True, text=True, cwd=ROOT)


# Each test returns (result, detail). Results: PASS, FAIL, INCONCLUSIVE, CONFIRM (a person must say what they saw).

def t_vnc_frame():
    f = frame("vnc_frame")
    return ("PASS", f"full frame {f.width}x{f.height}") if f.size == (1920, 1080) else ("FAIL", f"frame {f.size}")


def t_pointer_square():
    corners = [(560, 240), (1360, 240), (1360, 840), (560, 840), (560, 240)]
    actions = []
    for (x0, y0), (x1, y1) in zip(corners, corners[1:]):
        for i in range(1, 21):
            actions += [f"move:{x0 + (x1 - x0) * i // 20},{y0 + (y1 - y0) * i // 20}", "wait:0.1"]
    vnc(*actions, "move:960,540")
    return "CONFIRM", "did the pointer trace the 800 x 600 square clockwise and stop in the centre?"


def t_click_launch():
    before = frame("click_launch_before")
    vnc(f"click:{TILE['terminal'][0]},{TILE['terminal'][1]}", "wait:3")
    after = frame("click_launch_after")
    st = running("st")
    if share_changed(before, after) > 0.5 and st is not False:
        return "PASS", f"terminal opened ({share_changed(before, after):.0%} of the screen changed, st running: {st})"
    return "FAIL", f"{share_changed(before, after):.0%} of the screen changed, st running: {st}"


def t_keys():
    before = frame("keys_before")
    vnc("text:on", "wait:1")
    box = changed(before, frame("keys_after"))
    if box and box[2] - box[0] <= 2 * CELL_W + 2 and box[1] < LOGIN_Y < box[3]:
        return "PASS", f"'on' appeared after the prompt, changed box {box}"
    return "FAIL", f"changed box {box}"


def t_backspace():
    vnc("key:0xff08", "key:0xff08", "wait:1")
    box = changed(frame("keys_before"), frame("backspace_after"))
    return ("PASS", "frame identical to the one before typing") if box is None else ("FAIL", f"still differs: {box}")


def t_click_cursor():
    vnc("text:qnxuser", "wait:1")
    before = frame("click_cursor_before")
    x = LOGIN_X + 3 * CELL_W + 3  # inside the 'u' cell: a click there should put the cursor before 'u'
    vnc(f"click:{x},{LOGIN_Y}", "text:X", "wait:1")
    box = changed(before, frame("click_cursor_after"))
    vnc("key:0xff08")  # removes the X wherever the cursor is
    end = LOGIN_X + 7 * CELL_W
    if box and box[0] < end - CELL_W:
        return "PASS", f"X was inserted at the click (changed box starts at x={box[0]})"
    return "FAIL", f"X went to the end of the line, not to the click at x={x} (changed box {box})"


def t_drag_select():
    before = frame("drag_select_before")
    vnc(f"drag:{LOGIN_X + 2},{LOGIN_Y},{LOGIN_X + 7 * CELL_W - 2},{LOGIN_Y}", "wait:1")
    box = changed(before, frame("drag_select_after"))
    if box:
        return "PASS", f"the text changed under the drag (highlight), box {box}"
    return "FAIL", "no highlight: the frame did not change"


def t_login():
    before = frame("login_before")
    vnc(*["key:0xff08"] * 20, "text:qnxuser", "key:0xff0d", "wait:1.5",
        "text:" + need("ON_LOGIN_PASSWORD"), "key:0xff0d", "wait:3")
    share = share_changed(before, frame("login_after"))
    return ("PASS", f"logged in, welcome text shown ({share:.0%} of the screen changed)") if share > 0.02 \
        else ("FAIL", f"only {share:.1%} of the screen changed")


def t_capture():
    png, txt = RUNS / "motd_oculix.png", RUNS / "motd_ocr.txt"
    r = java("host/spikes/ocr/OcrScreen.java", HOST, need("ON_VNC_PASSWORD"), str(png), str(txt))
    if r.returncode or not png.exists():
        return "FAIL", (r.stdout + r.stderr).strip().splitlines()[-1:]
    return "PASS", f"OculiX captured {Image.open(png).size} and OCR'd it: " + \
        next((l for l in r.stdout.splitlines() if l.startswith("OCR:")), "")


def t_ocr_motd():
    r = subprocess.run([sys.executable, str(ROOT / "host/tools/ocr_compare.py"), str(EXPECTED_MOTD),
                        str(RUNS / "motd_ocr.txt")], capture_output=True, text=True)
    (RUNS / "motd_compare.txt").write_text(r.stdout)
    missing = [l[10:] for l in r.stdout.splitlines() if l.startswith("MISS")]
    summary = r.stdout.strip().splitlines()[-1]
    return ("PASS", summary) if r.returncode == 0 else ("FAIL", f"{summary}; missing: {missing}")


def t_camera():
    vnc("text:camera_example3_viewfinder", "key:0xff0d", "wait:4")
    a, b = frame("camera_1"), (time.sleep(1), frame("camera_2"))[1]
    live = changed(a, b) is not None
    return ("PASS", "the viewfinder runs, consecutive frames differ") if live else ("FAIL", "frames identical: not live")


def close_app(name, proc):
    before = frame(f"{name}_open")
    vnc(ALT_DEL, "wait:2")
    after = frame(f"{name}_closed")
    gone = not running(proc) if os.environ.get("ON_PI_SSH") else None
    if share_changed(before, after) > 0.2 and gone is not False:
        return "PASS", f"Alt+Del closed it (process gone: {gone})"
    return "FAIL", f"{share_changed(before, after):.0%} of the screen changed, process gone: {gone}"


def t_close_camera():
    return close_app("camera", "camera_example3_viewfinder")


def t_shutdown_to_launcher():
    """The way out of the terminal on this image (David, 2026-11-13): sudo shutdown, which reboots the Pi."""
    vnc("text:sudo shutdown", "key:0xff0d", "wait:2", "text:" + need("ON_LOGIN_PASSWORD"), "key:0xff0d")
    launcher = Image.open(RUNS / "vnc_frame.png").convert("RGB")
    t0 = time.time()
    time.sleep(20)
    while time.time() - t0 < 180:
        try:
            if share_changed(launcher, frame("after_shutdown")) < 0.01:
                return "PASS", f"the Pi rebooted to the welcome launcher in about {time.time() - t0:.0f} s"
        except subprocess.CalledProcessError:
            pass  # vncserv not up yet
        time.sleep(5)
    return "FAIL", "no welcome launcher within 180 s"


def tile_app(name, proc):
    vnc(f"click:{TILE[name][0]},{TILE[name][1]}", "wait:4")
    a, b = frame(f"{name}_1"), (time.sleep(1), frame(f"{name}_2"))[1]
    if changed(a, b) is None:
        return "FAIL", f"{name}: frames identical, not live"
    result, detail = close_app(name, proc)
    return result, f"{name} ran (consecutive frames differ); " + detail


def t_gears():
    return tile_app("gears", "gles3-gears")


def t_maze():
    return tile_app("maze", "gles2-maze")


def t_cube():
    return tile_app("cube", "vkcubepp")


def update_count(rearm):
    if not os.environ.get("ON_PI_SSH"):
        return "SKIP", "ON_PI_SSH not set"
    moves = ["wait:2", "move:300,300", "move:600,300", "move:900,500", "move:1200,600", "move:1500,800"]
    t = threading.Thread(target=vnc, args=moves)
    t.start()
    out = pi("rs /tmp/screen_probe watch 8" + (" rearm" if rearm else ""))
    t.join()
    pulses = next(int(l.split()[2]) for l in out.splitlines() if l.startswith("total updates"))
    updates = int([l for l in out.splitlines() if l.startswith("metrics")][-1].split("update=")[1].split()[0])
    return ("PASS" if pulses == updates else "FAIL"), f"{pulses} update pulses, Screen counted {updates} updates"


def t_update_count():
    return update_count(False)


def t_update_rearm():
    return update_count(True)


def t_oculix_click():
    # The pattern is cut from F01's frame of the welcome screen, so no image of QNX's is stored in the repo.
    pattern = RUNS / "gears_tile.png"
    x, y = TILE["gears"]
    Image.open(RUNS / "vnc_frame.png").crop((x - 58, y - 58, x + 58, y + 58)).save(pattern)
    r = java("host/spikes/s1/S1VncScreen.java", HOST, need("ON_VNC_PASSWORD"), "find", str(pattern), str(RUNS), "click")
    found = [l for l in r.stdout.splitlines() if l.startswith("Finder at similarity 0.99")]
    clicked = any(l.startswith("clicked") for l in r.stdout.splitlines())
    if not (found and "score" in found[0] and clicked):
        return "FAIL", (found or r.stdout.strip().splitlines()[-1:])
    a, b = frame("oculix_gears_1"), (time.sleep(1), frame("oculix_gears_2"))[1]
    if changed(a, b) is None:
        return "FAIL", "clicked, but no live gears"
    result, detail = close_app("oculix_gears", "gles3-gears")
    return result, f"found ({found[0].split(': ', 1)[1]}), click launched gears; " + detail


# (id, title, what the observer sees, prerequisites, function)
TESTS = [
    ("F01", "VNC connect and full frame", "nothing (start on the welcome screen)", [], t_vnc_frame),
    ("F02", "pointer moves", "the pointer traces a square, stops in the centre", ["F01"], t_pointer_square),
    ("F03", "click opens an app", "the Terminal tile opens st at the login prompt", ["F02"], t_click_launch),
    ("F04", "keys reach the app", "'on' typed after login:", ["F03"], t_keys),
    ("F05", "Backspace", "'on' erased", ["F04"], t_backspace),
    ("F06", "click puts the cursor", "'qnxuser' typed, a click on 'u', an X typed there, then erased",
     ["F03", "F05"], t_click_cursor),
    ("F07", "drag selects text", "a drag across 'qnxuser' highlights it", ["F06"], t_drag_select),
    ("U01", "unboxing: log in", "login as qnxuser, the welcome message appears", ["F05"], t_login),
    ("U02", "unboxing: OculiX capture and OCR", "nothing", ["U01"], t_capture),
    ("U03", "unboxing: welcome text is right for this release", "nothing", ["U02"], t_ocr_motd),
    ("A01", "camera viewfinder runs", "moving colour bars", ["U01"], t_camera),
    ("A02", "Alt+Del closes an app", "back to the terminal", ["A01"], t_close_camera),
    ("A03", "sudo shutdown returns to the launcher", "'sudo shutdown' typed, the Pi reboots to the welcome "
     "launcher", ["A02"], t_shutdown_to_launcher),
    ("A04", "Gears tile", "gears turn, then the welcome screen", ["F03", "A02"], t_gears),
    ("A05", "Maze tile", "the brick maze, then the welcome screen", ["F03", "A02"], t_maze),
    ("A06", "cube tile (vkcubepp)", "a spinning cube with LunarG's logo (keep it out of demo footage), then the "
     "welcome screen", ["F03", "A02"], t_cube),
    ("S01", "update pulses = Screen's update count", "the pointer jumps to 5 spots", ["F02"], t_update_count),
    ("S02", "the same with re-arming", "the same 5 jumps", ["S01"], t_update_rearm),
    ("O01", "OculiX finds the Gears tile and clicks it", "gears open, then the welcome screen",
     ["F01", "F03", "A02"], t_oculix_click),
]
BY_ID = {t[0]: t for t in TESTS}

# Tests that cannot show anything on this image: recorded as N/A with the reason, not run.
KNOWN_LIMITS = {
    "F06": "the quick-start image's terminal is sandboxed and ignores the mouse, even a real one (David, "
           "2026-11-13); test mouse text functions in an app that takes the mouse",
    "F07": "the quick-start image's terminal is sandboxed and ignores the mouse, even a real one (David, "
           "2026-11-13); test mouse text functions in an app that takes the mouse",
}


def label(tid):
    """ID and title together: the ID is the key, the title is what a person follows."""
    return f"{tid} ({BY_ID[tid][1]})"


def load():
    return json.loads(STATE.read_text()) if STATE.exists() else {"started": None, "results": {}, "baseline": None}


def save(state):
    RUNS.mkdir(parents=True, exist_ok=True)
    STATE.write_text(json.dumps(state, indent=1))


def health():
    """(ok, text): no new core dumps and every graphics process running. None if there is no SSH access."""
    if not os.environ.get("ON_PI_SSH"):
        return None, "not checked (ON_PI_SSH not set)"
    cores = pi("rs ls /data/var/dumper").split()
    down = [p for p in GRAPHICS if not running(p)]
    return cores, down


def run(tid, state):
    _, _, _, requires, fn = BY_ID[tid]
    results = state["results"]
    unhealthy = next((r for r in results.values() if r.get("unhealthy")), None)
    if tid in KNOWN_LIMITS:
        result, detail = "N/A", "known limitation: " + KNOWN_LIMITS[tid]
    elif unhealthy:
        result, detail = "SKIP", f"target unhealthy since {label(unhealthy['id'])}: {unhealthy['unhealthy']}"
    else:
        blocking = [(p, results.get(p, {}).get("result", "not run")) for p in requires
                    if results.get(p, {}).get("result") != "PASS"]
        if blocking:
            result = "SKIP"
            detail = "prerequisite " + ", ".join(f"{label(p)} {r}" for p, r in blocking)
        else:
            t0 = time.time()
            try:
                result, detail = fn()
            except Exception as e:  # a broken test is a failed test, never a silent pass
                result, detail = "FAIL", f"error: {e!r}"
            detail = f"{detail} [{time.time() - t0:.0f} s]"
    entry = {"id": tid, "result": result, "detail": str(detail), "at": datetime.now(timezone.utc).isoformat()}
    if result not in ("SKIP", "N/A"):
        cores, down = health()
        if cores is not None:
            new = sorted(set(cores) - set(state["baseline"] or []))
            if new or down:
                entry["unhealthy"] = f"new cores {new}, processes down {down}"
                entry["result"] = "FAIL"
    results[tid] = entry
    save(state)
    print(f"{label(tid)}: {entry['result']} - {entry['detail']}" +
          (f" - TARGET UNHEALTHY: {entry['unhealthy']}" if "unhealthy" in entry else ""))


def main():
    cmd = sys.argv[1] if len(sys.argv) > 1 else "list"
    state = load()
    if cmd == "reset" or state["started"] is None:
        state = {"started": datetime.now(timezone.utc).isoformat(), "results": {}, "baseline": None}
        cores, _ = health()
        state["baseline"] = cores
        save(state)
        if cmd == "reset":
            print(f"new run; core dumps already on the target: {cores}")
            return
    if cmd == "list":
        for n, (tid, title, sees, requires, _) in enumerate(TESTS, 1):
            r = state["results"].get(tid, {})
            print(f"{n:2}/{len(TESTS)}  {label(tid)}: {r.get('result') or 'not run'}")
            print(f"        you see: {sees}")
            print(f"        needs:   {'; '.join(label(p) for p in requires) or 'nothing'}")
    elif cmd == "next":
        pending = [t[0] for t in TESTS if t[0] not in state["results"]]
        waiting = [i for i, r in state["results"].items() if r["result"] == "CONFIRM"]
        if waiting:
            print("waiting for confirmation of " + "; ".join(label(i) for i in waiting) + ": suite.py confirm ID pass|fail")
        elif pending:
            run(pending[0], state)
        else:
            print("all tests have a result")
    elif cmd == "run":
        run(sys.argv[2], state)
    elif cmd == "confirm":
        tid, verdict = sys.argv[2], sys.argv[3].upper()
        entry = state["results"][tid]
        entry["result"] = verdict
        entry["detail"] += f"; observer: {verdict}" + (f" ({' '.join(sys.argv[4:])})" if sys.argv[4:] else "")
        save(state)
        print(f"{label(tid)}: {verdict}")
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
