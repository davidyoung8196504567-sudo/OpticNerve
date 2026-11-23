"""A hand on the mouse and keyboard, for every tool and test that sends input to a target: the one way input is sent.

The pointer glides from where it is to the target along a slightly curved path, easing in and out, at about 60
pointer events a second (0.5 to 0.8 s by distance, about 1.5 px of tremor), rests 80 to 200 ms, then clicks with the
button held 50 to 90 ms. A drag holds the button along such a path. Each key is held 40 to 80 ms and pressed 60 to
180 ms after the last one was released. The Java tests (host/demo/*.java) and the OculiX scripts (*.sikuli) carry
the same model (tests/regression/test_hand.py keeps the copies identical and their numbers the same as here).

The agent does not report where the pointer is, so the hand remembers it between runs (a small file per HOST:PORT in
~/.cache/opticnerve); the first move to a target it has not seen starts at the bottom centre of the screen, where a
hand comes in. A seed (ON_HAND_SEED, or the seed argument) repeats the paths only from the same start point: pass
start=(x, y) for that.

    h = Hand(send, width, height, "HOST:PORT")   send(bytes) writes to the RFB connection
    h.move(x, y); h.click(x, y); h.drag(x1, y1, x2, y2); h.keys([keysym, ...]); h.key(keysym)
"""
import math, os, random, struct, time

SEED = int(os.environ.get("ON_HAND_SEED") or time.time_ns() % 1000000)


class Hand:
    def __init__(self, send, width, height, target, seed=None, start=None):
        self.send, self.w, self.h = send, width, height
        self.rnd = random.Random(SEED if seed is None else seed)
        self.buttons = 0
        self.key_up = None   # when the last key was released
        self.state = os.path.join(os.path.expanduser("~"), ".cache", "opticnerve",
                                  "pointer-" + "".join(c if c.isalnum() or c in ".-" else "-" for c in target))
        self.pos = [width // 2, height - 1]
        if start is not None:
            self.pos = list(self._clamp(*start))
            return
        try:
            with open(self.state) as f:
                x, y = (int(v) for v in f.read().split(","))
            if 0 <= x < width and 0 <= y < height:
                self.pos = [x, y]
        except (OSError, ValueError):
            pass

    def _clamp(self, x, y):
        return max(0, min(self.w - 1, int(round(x)))), max(0, min(self.h - 1, int(round(y))))

    def _pointer(self, x, y):
        x, y = self._clamp(x, y)
        self.send(struct.pack(">BBHH", 5, self.buttons, x, y))
        return x, y

    def move(self, x1, y1):
        x1, y1 = self._clamp(x1, y1)
        x0, y0 = self.pos
        dist = math.hypot(x1 - x0, y1 - y0)
        if dist >= 1:
            dur = 0.5 + 0.3 * min(1.0, dist / 1500)
            bend = self.rnd.uniform(-0.15, 0.15) * dist
            nx, ny = -(y1 - y0) / dist, (x1 - x0) / dist
            cx, cy = (x0 + x1) / 2 + nx * bend, (y0 + y1) / 2 + ny * bend
            steps = max(2, int(dur * 60))
            for i in range(1, steps + 1):
                t = i / steps
                e = t * t * (3 - 2 * t)
                x = (1 - e) ** 2 * x0 + 2 * (1 - e) * e * cx + e * e * x1
                y = (1 - e) ** 2 * y0 + 2 * (1 - e) * e * cy + e * e * y1
                if i < steps:
                    x += self.rnd.uniform(-1.5, 1.5)
                    y += self.rnd.uniform(-1.5, 1.5)
                self._pointer(x, y)
                time.sleep(dur / steps)
        else:
            self._pointer(x1, y1)
        self.pos = [x1, y1]
        try:
            os.makedirs(os.path.dirname(self.state), exist_ok=True)
            with open(self.state, "w") as f:
                f.write(f"{x1},{y1}")
        except OSError:
            pass

    def click(self, x, y):
        self.move(x, y)
        x, y = self.pos
        time.sleep(self.rnd.uniform(0.08, 0.2))
        self.buttons = 1
        self._pointer(x, y)
        time.sleep(self.rnd.uniform(0.05, 0.09))
        self.buttons = 0
        self._pointer(x, y)

    def drag(self, x1, y1, x2, y2):
        self.move(x1, y1)
        time.sleep(self.rnd.uniform(0.08, 0.2))
        self.buttons = 1
        try:
            self._pointer(*self.pos)
            time.sleep(self.rnd.uniform(0.1, 0.2))
            self.move(x2, y2)
            time.sleep(self.rnd.uniform(0.08, 0.2))
        finally:
            self.buttons = 0
            self._pointer(*self.pos)

    def _gap(self):
        """60 to 180 ms after the last key was released, as a hand reaches for the next one."""
        if self.key_up is not None:
            wait = self.key_up + self.rnd.uniform(0.06, 0.18) - time.monotonic()
            if wait > 0:
                time.sleep(wait)

    def key(self, keysym, down=None):
        """Press and release a key; with down=True or False, only press or only release it."""
        if down is not False:
            self._gap()
        for d in ((1, 0) if down is None else (1 if down else 0,)):
            self.send(struct.pack(">BBxxI", 4, d, keysym))
            if down is None and d:
                time.sleep(self.rnd.uniform(0.04, 0.08))
        if down is not True:
            self.key_up = time.monotonic()

    def keys(self, keysyms):
        for k in keysyms:
            self.key(k)
