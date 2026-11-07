"""Canyon Rail (Next) - the game played through the harness.

Boots to the title (the autopilot riding behind it), turns the help pages,
starts stage 1, moves the sight by keys and fires (shots counted), pauses
and resumes, then hands over to the original's autopilot (B), which rides
the stage, shoots its targets and the guardian's four nodes: stage 2. The
autopilot is turned off and the bolts wear the shield down: game over, the
initials (BOT), back to the title. The game is recorded from reset and
replayed twice: every frame hash must match. Screenshots go to screenshots/;
the frame rate during the ride is printed.
"""
import os
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "zxdev" / "src"))

from zxdev.emu import Emulator  # noqa: E402

HERE = Path(__file__).resolve().parent
NEX = Path(os.environ.get("CANYON_NEX", HERE / "build" / "canyon_rail.nex"))   # (env: test a copy)
SHOTS = HERE / "screenshots"
REC = HERE / "build" / "bot-game.json"
failures = []

# CRGame fields (src/cr.h as zsdcc lays it out)
O_STAGE, O_CAM_S, O_SX, O_SY, O_SHIELD, O_SCORE, O_SHOTS, O_HITS = 8, 16, 1520, 1522, 1530, 1532, 1540, 1542


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}", flush=True)
    if not ok:
        failures.append(name)


class Game:
    def __init__(self, e):
        self.e = e
        self.g = e.addr("_game")
        self.log = ""
        self.since = 0

    def read(self):
        b = self.e.read_mem(self.g, 1550)
        h = lambda o: struct.unpack("<h", b[o:o + 2])[0]
        return dict(stage=h(O_STAGE), cam_s=struct.unpack("<i", b[O_CAM_S:O_CAM_S + 4])[0], sx=h(O_SX), sy=h(O_SY),
                    shield=h(O_SHIELD), score=struct.unpack("<i", b[O_SCORE:O_SCORE + 4])[0], shots=h(O_SHOTS),
                    hits=h(O_HITS))

    def run(self, n):
        self.e.run(n)
        self.log += self.e.log()

    def tap(self, key, after=30):
        self.e.press(key, hold_frames=4, release_frames=12)
        self.run(after)

    def mark(self):
        self.since = len(self.log)

    def until(self, text, max_frames, step=25):
        n = 0
        while text not in self.log[self.since:] and n < max_frames:
            self.run(step)
            n += step
        ok = text in self.log[self.since:]
        if ok:
            self.since += self.log[self.since:].index(text) + len(text)
        return ok


def fps_of(log):
    """frames a second while riding (the fps lines between start and over)"""
    v, riding = [], False
    for line in log.splitlines():
        if "canyon: start" in line:
            riding = True
        elif "canyon: state over" in line or "canyon: title" in line:
            riding = False
        m = re.match(r"canyon: fps\*10=(\d+)", line)
        if m and riding:
            v.append(int(m.group(1)))
    return sum(v) / len(v) / 10 if v else 0


SHOTS.mkdir(exist_ok=True)
with Emulator(machine="next") as e:
    e.load(NEX)
    e.record_input("start", from_reset=True)
    g = Game(e)
    check("boots to the title", g.until("canyon: ready", 600))
    g.run(120)
    e.screenshot(SHOTS / "title.png")
    g.mark()
    g.tap("H")
    check("H: the rules", g.until("canyon: help 1", 100))
    e.screenshot(SHOTS / "help.png")
    g.tap("H")
    g.tap("H")
    check("the help pages close after page 2", g.until("canyon: help 0", 200))
    g.mark()
    g.tap("SPACE", 60)
    check("FIRE starts stage 1", g.until("canyon: start stage 1", 200))
    s0 = g.read()
    e.hold("O")
    g.run(40)
    e.release()
    s1 = g.read()
    check("O moves the sight left", s1["sx"] < s0["sx"], f"{s0['sx']} -> {s1['sx']}")
    e.hold("SPACE")
    g.run(80)
    e.release()
    check("SPACE fires", g.read()["shots"] > s1["shots"], str(g.read()))
    g.mark()
    g.tap("P")
    check("P pauses", g.until("canyon: paused", 100))
    e.screenshot(SHOTS / "paused.png")
    g.tap("P")
    check("and resumes", g.until("canyon: resumed", 100))
    # the original's autopilot rides the stage
    g.mark()
    g.tap("B")
    check("B: the autopilot", g.until("canyon: autoplay 1", 100))
    g.run(300)
    e.screenshot(SHOTS / "ride.png")
    check("targets shot down", g.until("canyon: ev KILL", 3000))
    check("the guardian", g.until("canyon: ev BOSS ", 6000))
    g.run(150)
    e.screenshot(SHOTS / "guardian.png")
    check("its nodes shot out", g.until("canyon: ev NODE", 3000))
    check("stage clear", g.until("canyon: ev BOSS_DOWN", 3000))
    g.run(20)
    e.screenshot(SHOTS / "clear.png")
    check("stage 2", g.until("canyon: ev STAGE", 600))
    st = g.read()
    check("a score", st["score"] > 10000, str(st))
    # no autopilot, no shooting: the bolts wear the shield down
    g.mark()
    g.tap("B")
    check("the autopilot off", g.until("canyon: autoplay 0", 100))
    check("a bolt lands", g.until("canyon: ev HURT", 3000))
    e.screenshot(SHOTS / "hurt.png")
    check("the shield runs out: game over", g.until("canyon: ev OVER", 8000))
    g.run(60)
    e.screenshot(SHOTS / "gameover.png")
    g.run(60)
    g.mark()
    g.tap("SPACE", 40)
    if g.until("canyon: hiscore rank", 300):
        g.run(30)
        e.screenshot(SHOTS / "hiscore.png")
        e.type("BOT", hold_frames=4, gap_frames=12)
        check("high-score name entry", g.until("canyon: name=BOT", 400), g.log[-200:])
    else:
        check("a score for the table", False, str(g.read()))
    check("back to the title", g.until("canyon: title", 600))
    g.run(330)
    e.screenshot(SHOTS / "best.png")
    fps = fps_of(g.log)
    if failures:
        (HERE / "build" / "test-log.txt").write_text(g.log)
    rec = e.record_input("save", REC)
    print(f"  final score {st['score']}; {fps:.1f} frames a second riding (the rules tick at 25 Hz); "
          f"recorded {rec['frames']} frames, {rec['events']} input changes")

seqs = []
for i in range(2):
    with Emulator(machine="next") as e:
        r = e.replay(REC)
        seqs.append([h for _, h in r["hashes"]])
        check(f"replay {i + 1}: every frame hash matches the recorded game", r["ok"],
              f"{r.get('mismatches')} mismatches, first at {r.get('first_mismatch')}")
check("two replays give identical screens", seqs[0] == seqs[1] and len(seqs[0]) > 1000)

print(f"{'FAIL' if failures else 'PASS'}: canyon_rail")
sys.exit(1 if failures else 0)
