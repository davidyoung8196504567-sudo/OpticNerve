#!/usr/bin/env python3
"""Control OBS Studio on this host through its WebSocket server (protocol v5), with the standard library only.

    python host/tools/obs_control.py version
    python host/tools/obs_control.py inputs                  list the sources (inputs) and their kinds
    python host/tools/obs_control.py status                  is OBS recording, and for how long
    python host/tools/obs_control.py scene NAME              switch the program scene ("Scene": the Pi's monitor via the
                                                             webcam, "Scene 2": this laptop's display)
    python host/tools/obs_control.py start                   start recording
    python host/tools/obs_control.py stop LABEL              stop recording and rename the file to
                                                             OpticNerve_<UTC date-time>_<LABEL>.<ext>
    python host/tools/obs_control.py shot SOURCE OUT.png     one frame of SOURCE (e.g. the webcam) as PNG

OBS must run with Tools > WebSocket Server Settings > Enable WebSocket server. Port 4455 on localhost unless
ON_OBS_PORT is set. The password comes from ON_OBS_PASSWORD or else from OBS's own settings file
(%APPDATA%/obs-studio/plugin_config/obs-websocket/config.json); it is never printed.

OBS's profile writes every recording to the same name (OBS_recording.mp4, overwriting), and David renames each file
after it stops. So "start" refuses while a file of that name is still in the recording folder, and "stop" renames it.
"""
import base64, hashlib, json, os, re, socket, struct, sys, time, uuid
from datetime import datetime, timezone
from pathlib import Path


class Obs:
    def __init__(self, host="127.0.0.1", port=int(os.environ.get("ON_OBS_PORT", 4455))):
        self.s = socket.create_connection((host, port), timeout=30)
        key = base64.b64encode(os.urandom(16)).decode()
        self.s.sendall((f"GET / HTTP/1.1\r\nHost: {host}:{port}\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                        f"Sec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n"
                        "Sec-WebSocket-Protocol: obswebsocket.json\r\n\r\n").encode())
        head = b""
        while b"\r\n\r\n" not in head:
            head += self.s.recv(1)
        if b" 101 " not in head.split(b"\r\n")[0]:
            sys.exit(f"OBS refused the WebSocket upgrade: {head.split(b'\r\n')[0].decode()}")
        hello = self.recv()["d"]
        identify = {"rpcVersion": 1}
        if "authentication" in hello:
            a = hello["authentication"]
            secret = base64.b64encode(hashlib.sha256((password() + a["salt"]).encode()).digest())
            identify["authentication"] = base64.b64encode(
                hashlib.sha256(secret + a["challenge"].encode()).digest()).decode()
        self.send({"op": 1, "d": identify})
        if self.recv()["op"] != 2:
            sys.exit("OBS did not accept the identification (wrong password?)")

    def recvn(self, n):
        b = b""
        while len(b) < n:
            c = self.s.recv(n - len(b))
            if not c:
                raise EOFError("OBS closed the connection")
            b += c
        return b

    def send(self, msg):
        data = json.dumps(msg).encode()
        n = len(data)
        head = bytes([0x81]) + (bytes([0x80 | n]) if n < 126 else
                                bytes([0x80 | 126]) + struct.pack(">H", n) if n < 65536 else
                                bytes([0x80 | 127]) + struct.pack(">Q", n))
        mask = os.urandom(4)  # clients must mask every frame
        self.s.sendall(head + mask + bytes(b ^ mask[i % 4] for i, b in enumerate(data)))

    def recv(self):
        data = b""
        while True:
            b0, b1 = self.recvn(2)
            n = b1 & 0x7F
            if n == 126:
                n = struct.unpack(">H", self.recvn(2))[0]
            elif n == 127:
                n = struct.unpack(">Q", self.recvn(8))[0]
            payload = self.recvn(n)
            op = b0 & 0x0F
            if op == 8:
                raise EOFError(f"OBS closed the connection: {payload[2:].decode(errors='replace')}")
            if op == 9:  # ping
                self.s.sendall(bytes([0x8A, 0x80]) + os.urandom(4))
                continue
            data += payload
            if b0 & 0x80:
                return json.loads(data)

    def request(self, kind, data=None):
        rid = str(uuid.uuid4())
        self.send({"op": 6, "d": {"requestType": kind, "requestId": rid, "requestData": data or {}}})
        while True:
            msg = self.recv()
            if msg["op"] == 7 and msg["d"]["requestId"] == rid:
                status = msg["d"]["requestStatus"]
                if not status["result"]:
                    sys.exit(f"{kind} failed: {status.get('comment', status['code'])}")
                return msg["d"].get("responseData", {})


def password():
    if os.environ.get("ON_OBS_PASSWORD"):
        return os.environ["ON_OBS_PASSWORD"]
    cfg = Path(os.environ["APPDATA"]) / "obs-studio" / "plugin_config" / "obs-websocket" / "config.json"
    return json.loads(cfg.read_text())["server_password"]


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    obs, cmd = Obs(), sys.argv[1]
    if cmd == "version":
        v = obs.request("GetVersion")
        print(f"OBS {v['obsVersion']}, obs-websocket {v['obsWebSocketVersion']}, {v['platformDescription']}")
    elif cmd == "inputs":
        for i in obs.request("GetInputList")["inputs"]:
            print(f"{i['inputName']}  ({i['inputKind']})")
    elif cmd == "status":
        r = obs.request("GetRecordStatus")
        print(f"recording {r['outputActive']}, {r['outputTimecode']}, paused {r['outputPaused']}")
    elif cmd == "scene":
        obs.request("SetCurrentProgramScene", {"sceneName": sys.argv[2]})
        print(f"scene: {sys.argv[2]}")
    elif cmd == "start":
        folder = Path(obs.request("GetRecordDirectory")["recordDirectory"])
        name = obs.request("GetProfileParameter", {"parameterCategory": "Output",
                                                   "parameterName": "FilenameFormatting"})["parameterValue"]
        if "%" not in name:
            left = sorted(folder.glob(name + ".*"))
            if left:
                sys.exit(f"not started: {left[0]} is still there; rename or move it first")
        obs.request("StartRecord")
        print("recording started")
    elif cmd == "stop":
        if len(sys.argv) < 3:
            sys.exit("stop needs a LABEL for the file name")
        out = Path(obs.request("StopRecord")["outputPath"])
        time.sleep(2)  # let OBS finish writing the file
        stamp = datetime.now(timezone.utc).strftime("%Y-%m-%d_%H%MZ")
        label = re.sub(r"[^A-Za-z0-9._-]+", "-", "_".join(sys.argv[2:]))
        target = out.with_name(f"OpticNerve_{stamp}_{label}{out.suffix}")
        out.rename(target)
        print(f"recording stopped: {target} ({target.stat().st_size / 1e6:.1f} MB)")
    elif cmd == "shot":
        r = obs.request("GetSourceScreenshot", {"sourceName": sys.argv[2], "imageFormat": "png"})
        Path(sys.argv[3]).write_bytes(base64.b64decode(r["imageData"].split(",", 1)[1]))
        print(f"wrote {sys.argv[3]}")
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
