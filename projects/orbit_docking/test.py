"""Orbit Docking (Next) - plays a game through the harness.

1. The rules on the Z80N: with T held at start-up the game flies the
   original's long autopilot game (its tests/test_cases.c, seed 1234) and
   logs the state hash, which must be the original's tests/vectors.h.
2. A game, recorded from reset: the title (the autopilot docking behind it,
   music on two TurboSound chips), Kempston fire starts station 1, W
   thrusts in, K and L roll, the original's autopilot (B) docks at station
   1 (a score) and moves on to station 2; with the autopilot off, W held
   rams the station: crashes (the effect, a life lost each) until the game
   is over, then the initials for the high-score table and back to the
   title. The frame rate is measured while the autopilot flies.
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

NEX = HERE / "build" / "orbit_docking.nex"
SHOTS = HERE / "screenshots"
REC = HERE / "build" / "game.json"
VECTOR = re.search(r"0x([0-9A-F]{8})", (HERE / "tools" / "orig" / "tests" / "vectors.h").read_text()).group(1)
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


# ODGame (od.h) on the Z80: seed, rng (4 each), level lives over won between
# (2 each), then x y z vx vy vz (16.16), roll roll_rate (a8)
FIELDS = {"x": 18, "y": 22, "z": 26, "vx": 30, "vy": 34, "vz": 38, "roll": 42, "roll_rate": 46}


def field(e, name):
    off = FIELDS[name]
    return struct.unpack("<l", e.read_mem("_game", off + 4)[off:])[0]


def perf(log):
    """the perf lines: (fps x 10, ay volumes of chips 0, 1, 2)"""
    return [tuple(int(v) for v in m) for m in re.findall(r"perf fps10=(\d+) .*?ay=(\d+),(\d+),(\d+)", log)]


SHOTS.mkdir(exist_ok=True)

print("the rules (the original's long autopilot game)")
with Emulator(machine="next") as e:
    e.load(NEX)
    e.hold("T")
    ok = until(e, "od: selftest", 3000)
    e.release()
    m = re.search(r"selftest ticks=(\d+) level=(\d+) docks=(\d+) crashes=(\d+) score=(\d+) hash=([0-9A-F]+)", e.log(0))
    check("self-test ran", ok and m is not None)
    if m:
        check(f"4413 ticks to station 6, 5 docks, 3 crashes, score 13942, hash {VECTOR} (tests/vectors.h)",
              m.groups() == ("4413", "6", "5", "3", "13942", VECTOR), str(m.groups()))

print("a game")
with Emulator(machine="next") as e:
    e.load(NEX)
    e.record_input("start", from_reset=True)
    check("boots to the title", until(e, "od: state TITLE", 300))
    check("Kempston found", "kempston=1" in e.log(0))
    e.run(400)
    e.screenshot(SHOTS / "title.png")
    p = perf(e.log(0))
    check("the music plays on two TurboSound chips", bool(p) and p[-1][1] > 0 and p[-1][2] > 0, str(p[-1:]))
    # Kempston: up picks station 2, down station 1 again, fire starts
    e.press("UP")
    e.press("DOWN")
    e.press("FIRE")
    check("Kempston fire starts station 1", e.run_until("od: start level 1", 50))
    e.run(60)
    # the thrusters: W in, K / L roll
    z0 = field(e, "vz")
    e.hold("W")
    e.run(20)
    e.release()
    vz = field(e, "vz")
    check("W thrusts in (closing on the station)", vz < z0 and vz < 0, f"{z0} -> {vz}")
    r0 = field(e, "roll_rate")
    e.hold("L")
    e.run(10)
    e.release()
    r1 = field(e, "roll_rate")
    check("L rolls right", r1 > r0, f"{r0} -> {r1}")
    e.screenshot(SHOTS / "play.png")
    # the autopilot docks at station 1
    e.press("B", hold_frames=4)
    check("B: the autopilot on", e.run_until("od: autoplay 1", 20))
    mark = len(e.log(0))
    e.run(600)
    e.screenshot(SHOTS / "approach.png")
    ok = until(e, "od: ev DOCK", 4000)
    m = re.search(r"ev DOCK a=1 b=(\d+) score=(\d+)", e.log(0))
    check("docked at station 1, scored", ok and m is not None and int(m.group(2)) >= 1000, e.log(0)[-300:])
    fp = perf(e.log(0)[mark:])
    e.run(10)
    e.screenshot(SHOTS / "docked.png")
    check("on to station 2", e.run_until("od: ev LEVEL a=2", 200))
    e.press("B", hold_frames=4)
    check("B: the autopilot off", e.run_until("od: autoplay 0", 20))
    # W held: straight at the station, off its axis, far too fast: a crash a life
    e.hold("W")
    ok = e.run_until("od: ev CRASH", 2000)
    e.run(8)
    e.screenshot(SHOTS / "crash.png")
    m = re.search(r"ev CRASH a=(\d+) b=(\d+)", e.log(0))
    check("rammed the station: a crash, a life lost", ok and m is not None and m.group(2) == "2", e.log(0)[-200:])
    ok = e.run_until("od: state OVER", 4000)
    e.release()
    check("three crashes: game over", ok and len(re.findall(r"ev CRASH", e.log(0))) == 3)
    e.run(100)                                   # (fire counts after 40 ticks)
    e.screenshot(SHOTS / "gameover.png")
    e.press("SPACE")
    check("a high score: the initials", e.run_until("od: state ENTRY", 50))
    e.run(10)
    e.press("Z", hold_frames=4, release_frames=4)
    e.press("E", hold_frames=4, release_frames=4)
    e.screenshot(SHOTS / "initials.png")
    e.press("D", hold_frames=4, release_frames=4)
    check("initials ZED in the table", e.run_until("od: hiscore ZED", 50))
    check("back to the title", e.run_until("od: state TITLE", 50))
    e.run(30)
    e.screenshot(SHOTS / "hiscores.png")
    rec = e.record_input("save", REC)
    print(f"  recorded {rec['frames']} frames, {rec['events']} input changes")
    fps = [f[0] / 10 for f in fp]
    print(f"  frames a second while the autopilot flew station 1: {fps}")
    check("at least 12 frames a second", bool(fps) and min(fps) >= 12, str(fps))

seqs = []
for i in range(2):
    with Emulator(machine="next") as e:
        r = e.replay(REC)
        seqs.append([h for _, h in r["hashes"]])
        check(f"replay {i + 1}: every frame hash matches the recorded game", r["ok"],
              f"{r.get('mismatches')} mismatches, first at {r.get('first_mismatch')}")
check("two replays give identical screens", seqs[0] == seqs[1] and len(seqs[0]) > 2000)

print(f"{'FAIL' if failures else 'PASS'}: orbit_docking")
sys.exit(1 if failures else 0)
