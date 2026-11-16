"""Records one take of the demo video: the shots of take.sh in a full-screen terminal, with OBS recording.

    python host/demo/video/record_take.py NAME [--dry]      e.g. take11; --dry runs the shots without OBS

Windows with Git for Windows (bash and its mintty terminal) and OBS Studio with its WebSocket server; the OBS scene
"OpticNerve demo split" and the webcam settings are in docs/CONFIG_MANAGEMENT.md. Before: the agent installed on the
target and stopped (python host/tools/opticnerve_deploy.py install), and the environment that take.sh lists.

The order keeps the video clean: the terminal opens first and waits on a blank screen; one OBS connection is held
for the whole take, made before recording starts (OBS's "connected" alert fades first) and closed after recording
stops; nothing else talks to OBS during the take. The file lands in OBS's recording folder.
"""
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parents[1] / "tools"))
import obs_control as oc  # noqa: E402

MINTTY = r"C:\Program Files\Git\usr\bin\mintty.exe"

name = next((a for a in sys.argv[1:] if not a.startswith("--")), "take")
dry = "--dry" in sys.argv
flags = Path(tempfile.mkdtemp(prefix="opticnerve-take-"))
# Minimise every window first (as Win+D does; OBS keeps recording): Windows does not bring a window opened by a
# background program to the front, so any window left open would cover the terminal (take 11 of 2026-11-16).
subprocess.run(["powershell", "-NoProfile", "-Command", "(New-Object -ComObject Shell.Application).MinimizeAll()"],
               check=False)
time.sleep(1)
subprocess.Popen([MINTTY, "-w", "max", "-t", "OpticNerve demo", "-o", "FontHeight=40", "/usr/bin/bash", "-l", "-c",
                  "TAKE_DIR=$1 exec bash $2", "take", flags.as_posix(), (HERE / "take.sh").as_posix()])
time.sleep(5)
o = None
if not dry:
    o = oc.Obs()
    o.request("SetCurrentProgramScene", {"sceneName": "OpticNerve demo split"})
    time.sleep(10)  # OBS's connect alert fades
    oc.start_record(o)
    time.sleep(1.5)
(flags / "take_go").touch()
print("shots started" + ("" if dry else ", recording"), flush=True)
t0 = time.time()
while not (flags / "take_done").exists() and time.time() - t0 < 400:
    time.sleep(1)
print("take finished after %.0f s" % (time.time() - t0), flush=True)
if o:
    time.sleep(1)
    oc.stop_record(o, name)
    o.request("SetCurrentProgramScene", {"sceneName": "Scene"})
    o.close()
shutil.rmtree(flags, ignore_errors=True)
