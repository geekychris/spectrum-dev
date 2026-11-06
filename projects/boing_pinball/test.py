"""Boing Pinball (Next) - plays a whole game through the harness.

A bot reads the balls (`pin_probe`, by symbol) and plays with the keys the
way the original's demo player (pb_bot) does: it pulls the plunger back and
lets go, and flips when a ball comes down onto a flipper. Balls drain in
the end (three balls a game), so: start, score, lose balls, game over,
high-score entry, the title. The game is recorded and replayed twice
(every frame hash must match). Screenshots go to screenshots/.
"""
import math
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "zxdev" / "src"))

from zxdev.emu import Emulator  # noqa: E402

HERE = Path(__file__).resolve().parent
NEX = HERE / "build" / "boing_pinball.nex"
SHOTS = HERE / "screenshots"
REC = HERE / "build" / "bot-game.json"
# the flippers (pb_tables.c): pivot mm, length mm, rest angle; key
FLIPPERS = [((150, 126), 76, -0.52359, "Z"), ((328, 126), 76, 3.665176, "M")]
BS_PLAY, BS_LANE = 0, 1
failures = []
DEBUG = "--debug" in sys.argv


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


def probe(e):
    b = e.read_mem("pin_probe", 38)
    mode, n = struct.unpack("<bb", b[:2])
    v = struct.unpack("<16h", b[2:34])
    st = struct.unpack("<4b", b[34:38])
    balls = [dict(x=v[i] / 16, y=v[4 + i] / 16, vx=v[8 + i] / 4, vy=v[12 + i] / 4, state=st[i]) for i in range(max(0, min(n, 4)))]
    return mode, balls


def bot_game(e, frames=30000):
    """the original's pb_bot, with keys: returns the log"""
    log = ""
    hold = {"Z": 0, "M": 0}
    pull = 0
    t = 0
    shot = False
    while t < frames:
        mode, balls = probe(e)
        if mode != 1:                                      # not playing any more
            break
        keys = []
        lane = any(b["state"] == BS_LANE for b in balls)
        if lane:
            pull += 1
            if pull < 40:
                keys.append("SPACE")                       # pull back ~0.8 s, then let go
            elif pull > 44:
                pull = 0
        for b in balls:
            if b["state"] != BS_PLAY:
                continue
            for (px, py), ln, rest, key in FLIPPERS:
                tx, ty = px + ln * math.cos(rest), py + ln * math.sin(rest)
                dx, dy = b["x"] - (px + tx) / 2, b["y"] - (py + ty) / 2
                if b["vy"] < -60 and abs(dx) < ln * 3 / 4 + 13.5 and 0 < dy < 40 + abs(b["vy"]) / 20 * 2.4:
                    hold[key] = 12                         # 0.25 s
        for k in hold:
            if hold[k] > 0:
                keys.append(k)
                hold[k] -= 1
        if keys:
            e.hold(" ".join(keys))
        else:
            e.release()
        e.run(1)
        t += 1
        log += e.log()
        if not shot and t > 700 and any(b["y"] > 500 for b in balls):
            e.screenshot(SHOTS / "play.png")
            shot = True
    e.release()
    return log


with Emulator(machine="next") as e:
    e.load(NEX)
    check("boots to the title (the attract demo)", e.run_until("pinball: ready", 3000))
    check("TurboSound AY found", e.read_mem("ay_present", 1)[0] == 1)
    e.run(200)
    e.screenshot(SHOTS / "title.png")
    e.record_input("start", from_reset=True)
    e.run_until("pinball: ready", 3000)
    e.run(100)
    e.press("SPACE", hold_frames=2, release_frames=2)
    check("FIRE starts a game", e.run_until("pinball: start", 50))
    log = bot_game(e)
    scores = [int(s) for s in re.findall(r"pinball: score=(\d+)", log)]
    check("the bot scores points", scores and max(scores) >= 5000, str(scores[-3:]))
    check("bumpers, targets or ramps hit", any(k in log for k in ("pinball: bumper", "pinball: drop", "pinball: ramp")))
    drains = log.count("pinball: drain")
    check("balls drain (lives lost)", drains >= 3, str(drains))
    check("three balls, then game over", "pinball: gameover" in log, log[-300:])
    m = re.search(r"gameover score=(\d+) overruns=(\d+)", log)
    final = int(m.group(1)) if m else 0
    e.run(30)
    e.screenshot(SHOTS / "gameover.png")
    e.run(150)
    e.press("SPACE", hold_frames=2, release_frames=2)
    more = ""
    for _ in range(60):
        e.run(5)
        more += e.log()
        if "hiscore rank" in more or "pinball: title" in more:
            break
    if "hiscore rank" in more:
        e.run(20)
        e.type("BOT")
        e.run(10)
        more += e.log()
        check("high-score name entry", "pinball: name=BOT" in more, more[-200:])
        e.run_until("pinball: title", 300)
        more += "pinball: title"
    else:
        check(f"score {final} below the table: no entry", final <= 20000)
    check("back to the title", "pinball: title" in more)
    e.run(150 * 3)                                         # the display shows the scores in turn
    e.screenshot(SHOTS / "hiscores.png")
    rec = e.record_input("save", REC)
    print(f"  final score {final}, {drains} drains; recorded {rec['frames']} frames, {rec['events']} input changes")

seqs = []
for i in range(2):
    with Emulator(machine="next") as e:
        r = e.replay(REC)
        seqs.append([h for _, h in r["hashes"]])
        check(f"replay {i + 1}: every frame hash matches the recorded game", r["ok"],
              f"{r.get('mismatches')} mismatches, first at {r.get('first_mismatch')}")
check("two replays give identical screens", seqs[0] == seqs[1] and len(seqs[0]) > 1000)

print(f"{'FAIL' if failures else 'PASS'}: boing_pinball")
sys.exit(1 if failures else 0)
