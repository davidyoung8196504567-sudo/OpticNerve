"""Input is sent one way, the way a hand does it (host/tools/hand.py; the same model in the Java tests and the OculiX
IDE scripts). The Python hand is run against a fake connection; so is the IDE scripts' hand, under CPython with a
fake OculiX. The Java copies must be one identical block, with the model's numbers; outside the hand, no test sends
input to OculiX or to the wire directly.
"""
import math
import re
import struct
import sys
import types

import pytest

from conftest import ROOT

sys.path.insert(0, str(ROOT / "host" / "tools"))
import hand  # noqa: E402

JAVA = ["host/demo/DemoTest.java", "host/demo/DesktopTest.java", "host/demo/GtkDemoTest.java",
        "host/demo/LoginTest.java", "host/demo/ObstacleCourse.java", "host/spikes/s1/S1VncScreen.java"]
IDE = ["host/demo/DemoTest.sikuli/DemoTest.py", "host/ide-tests/Template.sikuli/Template.py"]
JSTART, JEND = "    // A hand on the mouse and keyboard", "    // (end of the hand)\n"
PSTART, PEND = "# A hand on the mouse and keyboard", "# (end of the hand)\n"


def block(src, start, end):
    a = src.index(start)
    b = src.index(end, a) + len(end)
    return src[a:b], src[:a] + src[b:]


@pytest.fixture
def wire(monkeypatch, tmp_path):
    """A Hand on a 1920x1080 screen whose messages are collected; no real waiting; its pointer file in tmp_path."""
    monkeypatch.setenv("HOME", str(tmp_path))
    monkeypatch.setenv("USERPROFILE", str(tmp_path))
    slept = []
    monkeypatch.setattr(hand.time, "sleep", slept.append)
    sent = []
    h = hand.Hand(sent.append, 1920, 1080, "test:1", seed=7)
    return h, sent, slept


def pointers(sent):
    return [struct.unpack(">BBHH", m) for m in sent if m[0] == 5]


def glides_like_a_hand(p, start, end, slept):
    """p: (buttons, x, y) of one move from start to end; slept: its waits."""
    assert math.hypot(p[0][1] - start[0], p[0][2] - start[1]) < 30       # begins where the pointer was
    assert (p[-1][1], p[-1][2]) == end                                    # ends exactly on the target
    assert len(p) >= 30                                                   # 0.5 s or more at about 60 events a second
    assert max(math.hypot(b[1] - a[1], b[2] - a[2]) for a, b in zip(p, p[1:])) < 30   # no jump along the way
    assert 0.5 <= sum(slept) <= 0.8 + 1e-9
    straight = [abs((end[1] - start[1]) * (x - start[0]) - (end[0] - start[0]) * (y - start[1]))
                / math.hypot(end[0] - start[0], end[1] - start[1]) for _, x, y in p]
    assert max(straight) <= 0.15 / 2 * math.hypot(end[0] - start[0], end[1] - start[1]) + 3   # bent, but little


def test_a_move_glides(wire):
    h, sent, slept = wire
    h.move(370, 800)
    p = [(b, x, y) for _, b, x, y in pointers(sent)]
    glides_like_a_hand(p, (960, 1079), (370, 800), slept)
    assert all(b == 0 for b, _, _ in p)


def test_a_click_rests_then_holds_the_button(wire):
    h, sent, slept = wire
    h.click(960, 800)
    p = pointers(sent)
    assert [b for _, b, _, _ in p[-2:]] == [1, 0] and p[-2][2:] == p[-1][2:] == (960, 800)
    assert 0.08 <= slept[-2] <= 0.2 and 0.05 <= slept[-1] <= 0.09    # the rest, then the hold


def test_a_drag_holds_the_button_along_the_path(wire):
    h, sent, _ = wire
    h.drag(100, 100, 600, 100)
    b = [m[1] for m in pointers(sent)]
    first, last = b.index(1), len(b) - 1 - b[::-1].index(1)
    assert all(v == 1 for v in b[first:last + 1]) and b[last + 1:] == [0]   # held without a break, released once
    assert last - first >= 30
    p = pointers(sent)
    assert p[first][2:] == (100, 100) and p[last][2:] == (600, 100)


def test_keys_are_held_and_come_apart(wire):
    h, sent, slept = wire
    h.keys([ord(c) for c in "abc"])
    h.key(0xff0d)                                                         # a separate key action waits too
    keys = [struct.unpack(">BBxxI", m) for m in sent if m[0] == 4]
    assert [(d, k) for _, d, k in keys] == [(1, 97), (0, 97), (1, 98), (0, 98), (1, 99), (0, 99), (1, 0xff0d), (0, 0xff0d)]
    holds, gaps = slept[0::2], slept[1::2]
    assert len(holds) == 4 and len(gaps) == 3
    assert all(0.04 <= s <= 0.08 for s in holds) and all(0.05 <= s <= 0.18 for s in gaps)


def test_the_pointer_is_remembered_and_clamped(wire):
    h, sent, _ = wire
    h.move(3000, -5)
    assert pointers(sent)[-1][2:] == (1919, 0)
    assert hand.Hand(sent.append, 1920, 1080, "test:1").pos == [1919, 0]
    assert hand.Hand(sent.append, 1920, 1080, "test:1", start=(960, 1000)).pos == [960, 1000]


# --- the Java copies ---------------------------------------------------------------------------------------------

def java_sources():
    return {p: (ROOT / p).read_text(encoding="utf-8") for p in JAVA}


def test_java_hand_is_one_block():
    blocks = {p: block(src, JSTART, JEND)[0] for p, src in java_sources().items()}
    first = blocks[JAVA[0]]
    for p, b in blocks.items():
        assert b == first, f"{p}: its hand differs from {JAVA[0]}'s"


def test_java_hand_has_the_model():
    b = block(java_sources()[JAVA[0]], JSTART, JEND)[0]
    for text in ("dur = 0.5 + 0.3 * Math.min(1, dist / 1500);", "(hand.nextDouble() * 0.3 - 0.15) * dist",
                 "Math.max(2, (int) (dur * 60))", "hand.nextDouble() * 3 - 1.5", "rest(0.08, 0.2);",
                 "ClickDelay = 0.05 + hand.nextDouble() * 0.04;", "rest(0.05, 0.09);", "rest(0.08, 0.15);",
                 "rest(0.06, 0.18);", "rest(0.04, 0.08);", "rest(0.06, 0.15);", "MoveMouseDelay = 0;"):
        assert text in b, text


@pytest.mark.parametrize("path", JAVA)
def test_java_sends_input_only_by_hand(path):
    rest = block((ROOT / path).read_text(encoding="utf-8"), JSTART, JEND)[1]
    rest = "\n".join(l for l in rest.splitlines() if not l.lstrip().startswith("//"))
    bypasses(rest, ("s", "screen"))


# Input calls on any OculiX object (a screen, a region, a match, a location), OculiX's Mouse, the robot, and the
# settings that change how OculiX moves and clicks. `write` only on a screen: ImageIO.write and Files.write are files.
INPUT = re.compile(r"[\w)\]]\.(click|doubleClick|rightClick|hover|type|paste|keyDown|keyUp|mouseMove|mouseDown|mouseUp|"
                   r"dragDrop|drag|dropAt|wheel)\(")
OTHER = re.compile(r"\bMouse\.\w+\(|getRobot\(|ClickDelay|MoveMouseDelay|TypeDelay")


def bypasses(rest, screens):
    found = INPUT.findall(rest) + OTHER.findall(rest)
    found += re.findall(r"(?<![\w.])(?:%s)\.write\(" % "|".join(screens), rest)
    assert not found, found


# --- the OculiX IDE scripts --------------------------------------------------------------------------------------

def ide_block(path):
    return block((ROOT / path).read_text(encoding="utf-8"), PSTART, PEND)


def test_ide_hand_is_one_block():
    assert ide_block(IDE[0])[0] == ide_block(IDE[1])[0]


@pytest.mark.parametrize("path", IDE)
def test_ide_sends_input_only_by_hand(path):
    rest = "\n".join(l for l in ide_block(path)[1].splitlines() if not l.lstrip().startswith("#"))
    bare = re.findall(r"(?<![\w.])(click|doubleClick|rightClick|hover|type|write|paste|keyDown|keyUp|mouseMove|"
                      r"mouseDown|mouseUp|dragDrop|wheel)\(", rest)
    assert not bare, bare
    bypasses(rest, ("scr", "vnc"))


def test_ide_hand_glides(monkeypatch):
    """The IDE scripts' hand, run under CPython with a fake OculiX screen of 1920x1080 at 0,0."""
    moves, clicks, keys, slept = [], [], [], []

    class Robot:
        def mouseMove(self, x, y):
            moves.append((x, y))

    class Scr:
        def getX(self): return 0
        def getY(self): return 0
        def getW(self): return 1920
        def getH(self): return 1080
        def getRobot(self): return Robot()
        def newLocation(self, x, y): return (x, y)
        def click(self, loc): clicks.append((loc, settings.ClickDelay))
        def keyDown(self, k): keys.append(("down", k))
        def keyUp(self, k): keys.append(("up", k))

    settings = types.SimpleNamespace(MoveMouseDelay=0.5, ClickDelay=0)
    fake_time = types.SimpleNamespace(sleep=slept.append)
    g = {"Settings": settings, "scr": Scr(), "vnc": True, "Location": None}
    exec(ide_block(IDE[0])[0], g)
    g["time"] = fake_time
    g["random"].seed(7)   # the jump limit holds for this path; unseeded, the tremor broke it about 1 run in 400
    assert settings.MoveMouseDelay == 0
    g["hand_click"](370, 800)
    p = [(0, x, y) for x, y in moves]
    glides_like_a_hand(p, (960, 1079), (370, 800), slept[:-1])
    assert clicks == [((370, 800), clicks[0][1])] and 0.05 <= clicks[0][1] <= 0.09
    assert 0.08 <= slept[-1] <= 0.2
    del slept[:]
    g["hand_type"]("ab")
    assert keys == [("down", "a"), ("up", "a"), ("down", "b"), ("up", "b")]
    assert all(0.06 <= s <= 0.18 for s in slept[0::2]) and all(0.04 <= s <= 0.08 for s in slept[1::2])


def test_ide_hand_jumps_without_glides():
    """glides = False (DemoTest.sikuli without ON_HAND=1): one move straight to the target, a click, no waiting."""
    moves, clicks, slept = [], [], []

    class Robot:
        def mouseMove(self, x, y):
            moves.append((x, y))

    class Scr:
        def getX(self): return 0
        def getY(self): return 0
        def getW(self): return 1920
        def getH(self): return 1080
        def getRobot(self): return Robot()
        def newLocation(self, x, y): return (x, y)
        def click(self, loc): clicks.append((loc, settings.ClickDelay))

    settings = types.SimpleNamespace(MoveMouseDelay=0.5, ClickDelay=0)
    g = {"Settings": settings, "scr": Scr(), "vnc": True, "Location": None}
    exec(ide_block(IDE[0])[0], g)
    g["time"] = types.SimpleNamespace(sleep=slept.append)
    g["glides"] = False
    g["hand_click"](2000, 800)
    assert moves == [(1919, 800)] and slept == []
    assert clicks == [((1919, 800), clicks[0][1])] and 0.05 <= clicks[0][1] <= 0.09


# --- the Python tools --------------------------------------------------------------------------------------------

def test_python_tools_send_input_only_through_the_hand():
    packs = re.compile(r"""pack\(\s*["'][>!]?\s*B\s*B\s*(H\s*H|x\s*x\s*I)\s*["']""")
    message = re.compile(r"bytes\(\s*\[\s*[45]\s*,")    # a PointerEvent (5) or KeyEvent (4) built by hand
    for folder in ("host", "tests"):
        for f in (ROOT / folder).rglob("*.py"):
            if f.name in ("hand.py", "test_hand.py") or "__pycache__" in f.parts:
                continue
            src = f.read_text(encoding="utf-8")
            assert not packs.search(src) and not message.search(src), f"{f.relative_to(ROOT)} sends input itself"
