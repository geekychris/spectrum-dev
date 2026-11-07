"""ScAIry Fun (Next) - a game played through the harness.

A bot reads the world (the `world` struct, the original's World laid out as
zsdcc packs it) and plays like the original's test drivers
(tests/test_cases.c careful()): on the Levee Road it takes the wheel before
the first flood, crosses it slowly in the lane centre and hands back (a good
catch, +500); it leaves the old lane lines and the second flood to the
autopilot (it rolls down the embankment and splashes into the river: the tow
truck, cover lost) and hands back after each; it looks through the cockpit
view on the way. At the marina: the arrival, the software update, Rush
Hour, where the autopilot's blind spots (the overpass shadows, cones, the
ambulance, debris) cost the rest of the cover: game over. Then the
initials (BOT) and the title. The rules pages, pause and the views are
tried too. The game is recorded from reset and replayed twice: every frame
hash must match. Screenshots go to screenshots/; the frame rate is printed.
"""
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "zxdev" / "src"))

from zxdev.emu import Emulator  # noqa: E402

HERE = Path(__file__).resolve().parent
NEX = HERE / "build" / "scairy_fun.nex"
SHOTS = HERE / "screenshots"
REC = HERE / "build" / "bot-game.json"
failures = []

# World field offsets (sc.h, packed as zsdcc lays it out: no padding)
O_ROUTE, O_FINISH, O_STATE, O_TIMER, O_VIEW = 0, 5342, 5344, 5348, 5350
O_CAR_STATE, O_CAR_Z, O_CAR_D, O_CAR_V = 5352, 5354, 5358, 5360
O_AI_ON, O_SCORE, O_COVER = 5436, 6516, 6520
GS = ["title", "intro", "play", "arrived", "update", "gameover", "win"]


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


class Game:
    def __init__(self, e):
        self.e = e
        self.w = e.addr("world")
        self.held = set()
        self.log = ""

    def read(self):
        b = self.e.read_mem(self.w + O_STATE, 8)
        state, _frame, timer, view = struct.unpack("<4h", b)
        c = self.e.read_mem(self.w + O_CAR_STATE, 10)
        cstate, z, d, v = struct.unpack("<hihh", c)
        route = struct.unpack("<h", self.e.read_mem(self.w + O_ROUTE, 2))[0]
        ap = struct.unpack("<h", self.e.read_mem(self.w + O_AI_ON, 2))[0]
        score, cover = struct.unpack("<ii", self.e.read_mem(self.w + O_SCORE, 8))
        return dict(state=GS[state], timer=timer, view=view, cstate=cstate, seg=z >> 11, off=(d >> 4),
                    kmh=(v * 90) >> 8, route=route, ap=ap, score=score, cover=cover)

    def keys(self, want):
        want = set(want)
        if want != self.held:
            self.e.release()
            if want:
                self.e.hold("+".join(sorted(want)) if len(want) > 1 else next(iter(want)))
            self.held = want

    def tap(self, key, frames=6):
        self.keys([])
        self.e.press(key, hold_frames=4, release_frames=12)
        self.run(frames - 16)

    def run(self, n):
        if n > 0:
            self.e.run(n)
        self.log += self.e.log()

    def mark(self):
        """a point in the log for until() to look from"""
        self.since = len(self.log)

    def until(self, text, max_frames):
        """text logged since the last mark() (or appearing within max_frames)"""
        start = getattr(self, "since", 0)
        n = 0
        while text not in self.log[start:] and n < max_frames:
            self.run(10)
            n += 10
        ok = text in self.log[start:]
        if ok:
            self.since = start + self.log[start:].index(text) + len(text)
        return ok


def careful(g, s, lane_centre=28, kmh=45):
    """the original's careful() driver: brake out of the autopilot, keep the
    speed, steer back to the lane centre"""
    k = []
    if s["ap"]:
        return ["A"]
    if s["kmh"] > kmh:
        k.append("A")
    elif s["kmh"] < kmh - 5:
        k.append("Q")
    off = s["off"] - lane_centre
    if off > 3:
        k.append("O")
    elif off < -3:
        k.append("P")
    return k


def hand_back(g, lane_centre=28):
    """the autopilot back on: near the lane centre, Space"""
    for _ in range(200):
        s = g.read()
        if s["ap"] or s["state"] != "play":
            return True
        off = s["off"] - lane_centre
        if abs(off) <= 10 and s["cstate"] == 0:
            g.tap("SPACE")
            continue
        g.keys(["O"] if off > 0 else ["P"] if s["kmh"] > 5 else ["Q"])
        g.run(2)
    return False


# the original's hazards that the autopilot gets wrong (track.c): first segment, length
FAILS = {0: [(108, 12), (152, 26), (352, 10)],
         1: [(100, 2), (170, 36), (236, 0), (300, 2), (360, 0)]}


def next_fail(route, seg):
    """the end of the first failing hazard that isn't behind seg"""
    for start, ln in FAILS.get(route, []):
        if start + ln + 2 >= seg:
            return start + ln + 3
    return -1


def drive(g, max_frames, until_state=None, shots=None, saves=(), view_at=None):
    """let the autopilot drive; take the hazards whose start is in `saves` by
    hand (the original's careful driver); after the tow truck, take the
    hazard that caught it by hand (the original puts the car back before it
    for that). Returns when the state is until_state."""
    shots = dict(shots or {})
    frames, viewed, manual_until = 0, False, -1
    while frames < max_frames:
        s = g.read()
        if until_state and s["state"] == until_state:
            break
        if s["state"] != "play":
            g.keys([])
            g.run(8)
            frames += 8
            continue
        for seg_at, name in list(shots.items()):
            if s["seg"] >= seg_at:
                g.e.screenshot(SHOTS / name)
                del shots[seg_at]
        if view_at and not viewed and s["seg"] >= view_at and s["ap"]:
            g.tap("V", 8)                       # the cockpit
            g.run(150)
            g.e.screenshot(SHOTS / "cockpit.png")
            g.tap("V", 8)
            viewed = True
            frames += 166
            continue
        if manual_until < 0:
            for start, ln in FAILS.get(s["route"], []):
                if start in saves and start - 8 <= s["seg"] <= start + ln + 2:
                    manual_until = start + ln + 3
        if manual_until >= 0:
            if s["seg"] < manual_until and s["cstate"] in (0, 1):
                g.keys(careful(g, s))
                g.run(2)
                frames += 2
                continue
            if s["seg"] >= manual_until:
                g.keys([])
                hand_back(g)
                manual_until = -1
                continue
        if not s["ap"] and s["cstate"] == 0:
            # stopped by the tow truck: drive the hazard ahead by hand, then hand back
            manual_until = next_fail(s["route"], s["seg"])
            if manual_until < 0:
                hand_back(g)
            continue
        g.keys([])
        g.run(10)
        frames += 10
    g.keys([])
    return frames


def fps_of(log):
    """frames a second while a route is being driven (the 3D view)"""
    v, playing = [], False
    for line in log.splitlines():
        m = re.match(r"scairy: state (\w+)", line)
        if m:
            playing = m.group(1) == "play"
        m = re.match(r"scairy: fps\*10=(\d+)", line)
        if m and playing:
            v.append(int(m.group(1)))
    return sum(v) / len(v) / 10 if v else 0


with Emulator(machine="next") as e:
    e.load(NEX)
    g = Game(e)
    check("boots to the title", g.until("scairy: title", 3000))
    e.record_input("start", from_reset=True)
    g = Game(e)
    g.until("scairy: title", 3000)
    g.run(120)
    e.screenshot(SHOTS / "title.png")
    # the rules: H turns the pages, then back
    g.tap("H", 30)
    check("H shows the rules", "help page 1" in g.log)
    e.screenshot(SHOTS / "rules.png")
    for _ in range(3):
        g.tap("H", 12)
    check("the rules close after page 3", "help page 0" in g.log)
    g.mark()
    g.tap("SPACE", 40)
    g.until("state intro route 1", 200)
    check("SPACE starts route 1", "state intro route 1" in g.log, g.log[-300:])
    e.screenshot(SHOTS / "intro.png")
    g.tap("SPACE", 20)
    check("and the autopilot drives", g.until("state play", 200) or "state play" in g.log)
    g.tap("S", 30)                              # pause
    check("pause", "scairy: paused" in g.log)
    e.screenshot(SHOTS / "paused.png")
    g.tap("S", 12)
    check("and resume", "scairy: resumed" in g.log)

    drive(g, 60000, until_state="arrived", shots={60: "levee.png", 112: "flood.png", 160: "old-lines.png"},
          saves=(108,), view_at=280)
    log = g.log
    check("the autopilot scores", g.read()["score"] > 1000, str(g.read()))
    check("taking over before the flood: a good catch", "ev save" in log, log[-400:])
    check("the old lane lines: over the embankment", "ev roll" in log)
    check("the second flood: into the river", "ev splash" in log)
    check("the tow truck puts the car back", "ev respawn" in log)
    check("the cockpit view", "view cockpit" in log and "view chase" in log)
    check("arrived at the marina", "ev arrived" in log)
    g.mark()
    g.run(60)
    e.screenshot(SHOTS / "arrived.png")
    check("then the software update", g.until("state update", 1500))
    g.run(150)
    e.screenshot(SHOTS / "update.png")
    g.mark()
    g.tap("SPACE", 20)
    check("route 2: Rush Hour", g.until("state intro route 2", 800))
    g.tap("SPACE", 20)
    drive(g, 60000, until_state="gameover", shots={60: "rush-hour.png"})
    log = g.log
    check("Rush Hour's blind spots cost the cover", "ev rearend" in log or "ev ticket" in log or "ev debris" in log)
    check("cover gone: game over", "ev game_over" in log, log[-300:])
    g.run(80)
    e.screenshot(SHOTS / "gameover.png")
    s = g.read()
    for _ in range(40):                         # until PRESS FIRE
        if g.read()["timer"] == 0:
            break
        g.run(10)
    g.mark()
    g.tap("SPACE", 30)
    if g.until("hiscore rank", 300):
        g.run(30)
        e.screenshot(SHOTS / "hiscore.png")
        e.type("BOT", hold_frames=4, gap_frames=12)
        check("high-score name entry", g.until("scairy: name=BOT", 400), g.log[-200:])
    else:
        check("a score for the table", False, f"score {s['score']}")
    check("back to the title", g.until("scairy: title", 600))
    g.run(100)
    fps = fps_of(g.log)
    rec = e.record_input("save", REC)
    print(f"  final score {s['score']}; {fps:.1f} frames a second (the 3D view; the simulation runs at 25 Hz); "
          f"recorded {rec['frames']} frames, {rec['events']} input changes")

seqs = []
for i in range(2):
    with Emulator(machine="next") as e:
        r = e.replay(REC)
        seqs.append([h for _, h in r["hashes"]])
        check(f"replay {i + 1}: every frame hash matches the recorded game", r["ok"],
              f"{r.get('mismatches')} mismatches, first at {r.get('first_mismatch')}")
check("two replays give identical screens", seqs[0] == seqs[1] and len(seqs[0]) > 1000)

print(f"{'FAIL' if failures else 'PASS'}: scairy_fun")
sys.exit(1 if failures else 0)
