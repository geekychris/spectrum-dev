"""Lander 3D (Next) - plays a game through the harness.

1. The rules on the Z80N: with T held at start-up the game flies three of
   flight_hash.h's flights (the autopilot on the Moon, a pilot's inputs on
   Mars and the asteroid) and logs their state hashes, which must be the
   original's (build/levels/expect.txt, made by the original's code).
2. A game, recorded from reset: the title (the autopilot's demo, the
   original's music on two TurboSound chips), Kempston right / left / fire
   starts the Moon; SPACE gives full thrust, W tips forward, E sets the
   steady throttle; the autopilot (B) lands (a score) and the next level
   starts; with the autopilot off and no thrust the lander falls and
   crashes, a ship each time, until the game is over; the initials go into
   the high-score table and the title comes back. The frame rate is
   measured while the autopilot flies and while the lander falls.
3. The recording replayed twice: every frame hash must match.
Screenshots go to screenshots/.
"""
import re
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / "tools" / "zxdev" / "src"))

from zxdev.emu import Emulator  # noqa: E402

NEX = HERE / "build" / "lander_3d.nex"
SHOTS = HERE / "screenshots"
REC = HERE / "build" / "game.json"
EXPECT = {tuple(int(v) for v in m[:3]): m[3:] for m in
          (line.split()[1:] for line in (HERE / "build" / "levels" / "expect.txt").read_text().splitlines())}
failures = []


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


def until(e, text, frames):
    """run until a log line starts, then a frame more (the whole line)"""
    ok = e.run_until(text, frames)
    e.run(1)
    return ok


# Level (ld.h) on the Z80: world level (2 each) seed (4) params (6 x 4) T (84), then the craft: p[3] v[3] pitch roll ...
CRAFT = 2 + 2 + 4 + 24 + 84
# HudVals (game.h): agl vs hs tilt lim_vs lim_hs lim_tilt (4 each) pad_dist (4) pad_dir wind_dir (2 each) fuel thr
HV_THR = 7 * 4 + 4 + 4 + 1


def craft(e, name):
    off = CRAFT + {"y": 4, "vy": 16, "pitch": 24}[name]
    return struct.unpack("<l", e.read_mem("_level", off + 4)[off:])[0]


def thr(e):
    return e.read_mem("_hv", HV_THR + 1)[HV_THR]


def perf(log):
    """the perf lines: (fps x 10, ay volumes of chips 0, 1, 2)"""
    return [tuple(int(v) for v in m) for m in re.findall(r"perf fps10=(\d+) .*?ay=(\d+),(\d+),(\d+)", log)]


SHOTS.mkdir(exist_ok=True)

print("the rules (the original's flights)")
with Emulator(machine="next") as e:
    e.load(NEX)
    e.hold("T")
    ok = until(e, "ld: state TITLE", 20000)
    e.release()
    got = re.findall(r"selftest flight (\d+) (\d+) (\d+) (\d+) (\d+) (\d+) ([0-9A-F]+)", e.log(0))
    check("self-test ran (3 flights)", ok and len(got) == 3, e.log(0)[-300:])
    for g in got:
        key = tuple(int(v) for v in g[:3])
        exp = EXPECT.get(key)
        check(f"flight {key}: {g[3]} steps, outcome {g[4]}, score {g[5]}, hash {g[6]} = the original's",
              exp is not None and list(g[3:]) == exp, f"the original: {exp}")

print("a game")
with Emulator(machine="next") as e:
    e.load(NEX)
    e.record_input("start", from_reset=True)
    check("boots to the title", until(e, "ld: state TITLE", 400))
    check("Kempston found", "kempston=1" in e.log(0))
    e.run(400)
    e.screenshot(SHOTS / "title.png")
    p = perf(e.log(0))
    check("the music plays on two TurboSound chips", any(q[2] for q in p) and any(q[3] for q in p), str(p))
    title_fps = [f[0] / 10 for f in p[1:]]
    # Kempston: right picks Mars, left the Moon again, fire starts
    e.press("RIGHT")
    e.press("LEFT")
    e.press("FIRE")
    check("Kempston fire starts the Moon, level 1", until(e, "ld: level MOON 1", 50))
    e.run(30)
    e.screenshot(SHOTS / "play.png")
    # the controls: SPACE full thrust, W tips forward, E the steady throttle
    v0 = craft(e, "vy")
    e.hold("SPACE")
    e.run(25)
    t_full = thr(e)
    e.release()
    v1 = craft(e, "vy")
    check("SPACE: full thrust (the descent slows)", t_full == 100 and v1 > v0, f"thr {t_full}, vy {v0} -> {v1}")
    p0 = craft(e, "pitch")
    e.hold("W")
    e.run(10)
    e.release()
    p1 = craft(e, "pitch")
    check("W tips forward", p1 > p0, f"{p0} -> {p1}")
    for _ in range(3):                           # (a pass takes up to ~15 frames: taps further apart)
        e.press("E", hold_frames=3, release_frames=24)
    e.run(20)
    check("E E E: the steady throttle at 3 x 0.1 (29%: the original's 16.16 0.1 x 3, x 100, truncated)", thr(e) == 29,
          str(thr(e)))
    # the autopilot lands
    mark = len(e.log(0))
    e.press("B", hold_frames=4)
    e.run(40)
    check("B: the autopilot on", "ld: autopilot 1" in e.log(0)[mark:])
    e.run(700)
    e.screenshot(SHOTS / "approach.png")
    ok = until(e, "ld: outcome", 6000)
    m = re.search(r"outcome (\w+) score=(\d+) total=(\d+)", e.log(0)[mark:])
    check("the autopilot landed on a pad, scored", ok and m is not None and m.group(1) == "landed" and int(m.group(2)) >= 100,
          e.log(0)[-300:])
    auto_fps = [f[0] / 10 for f in perf(e.log(0)[mark:])]
    e.run(10)
    e.screenshot(SHOTS / "landed.png")
    check("on to level 2", until(e, "ld: level MOON 2", 200))
    mark = len(e.log(0))
    e.press("B", hold_frames=4)
    e.run(40)
    check("B: the autopilot off", "ld: autopilot 0" in e.log(0)[mark:])
    # no thrust: it falls and crashes, a ship each time; three: game over
    mark = len(e.log(0))
    ok = until(e, "ld: outcome", 3000)
    m = re.search(r"outcome (\w+) .*?ships=(\d+)", e.log(0)[mark:])
    check("no thrust: crashed, a ship lost", ok and m is not None and m.group(1) == "crashed" and m.group(2) == "2",
          e.log(0)[-300:])
    fall_fps = [f[0] / 10 for f in perf(e.log(0)[mark:])]
    e.run(8)
    e.screenshot(SHOTS / "crash.png")
    ok = until(e, "ld: state OVER", 8000)
    check("three crashes: game over", ok and len(re.findall(r"outcome crashed", e.log(0))) == 3)
    e.run(160)                                   # (fire counts after 50 ticks)
    e.screenshot(SHOTS / "gameover.png")
    e.press("SPACE")
    check("a high score: the initials", until(e, "ld: state ENTRY", 60))
    e.run(10)
    e.press("Z", hold_frames=4, release_frames=10)
    e.press("E", hold_frames=4, release_frames=10)
    e.screenshot(SHOTS / "initials.png")
    e.press("D", hold_frames=4, release_frames=10)
    check("initials ZED in the table", until(e, "ld: hiscore ZED", 60))
    check("back to the title", until(e, "ld: state TITLE", 60))
    e.run(30)
    e.screenshot(SHOTS / "hiscores.png")
    log_play = e.log(0)
    log_play = log_play[log_play.index("ld: level MOON 1"):log_play.index("ld: state OVER")]
    rec = e.record_input("save", REC)
    print(f"  recorded {rec['frames']} frames, {rec['events']} input changes")
    print(f"  frames a second: title {title_fps}, the autopilot flying {auto_fps}, falling {fall_fps}")
    ticks = [(int(t), int(f)) for t, f in re.findall(r"perf fps10=\d+ ticks=(\d+) frames=(\d+)", log_play)]
    ok_ticks = [abs(2 * t - f) <= 2 for t, f in ticks]
    check("the rules keep the original's 25 ticks a second while flying (fixed timestep: a tick per 2 frames;"
          " a landing's pass can take longer than the catch-up of 10)",
          bool(ticks) and sum(ok_ticks) >= 0.9 * len(ticks), str(ticks))
    check("the picture: at least 1.5 frames a second while flying (see the README: below the 12 aimed at)",
          bool(auto_fps + fall_fps) and min(auto_fps + fall_fps) >= 1.5, str(auto_fps + fall_fps))

seqs = []
for i in range(2):
    with Emulator(machine="next") as e:
        r = e.replay(REC)
        seqs.append([h for _, h in r["hashes"]])
        check(f"replay {i + 1}: every frame hash matches the recorded game", r["ok"],
              f"{r.get('mismatches')} mismatches, first at {r.get('first_mismatch')}")
check("two replays give identical screens", seqs[0] == seqs[1] and len(seqs[0]) > 2000)

print(f"{'FAIL' if failures else 'PASS'}: lander_3d")
sys.exit(1 if failures else 0)
