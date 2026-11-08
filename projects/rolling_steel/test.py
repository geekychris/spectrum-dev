"""Rolling Steel (Next) - checks the rules against the original and plays a game.

1. The 32-bit maths (rsmath.asm, held T at start-up): its self-test hash is
   tools/test_rsmath.py's (C's long semantics).
2. The rules: the original's C (tools/orig: game.c, phys.c, course.c) runs
   on the host (tools/hostsim.c, a 32-bit long) from game_init() with no
   input: the title's attract demo plays the courses one after another. The
   Next (H held at start-up: hash the race) must log the same race-state
   hash at every change of state: the same physics, step for step.
3. A game, recorded from reset: the title (music on two TurboSound chips),
   Kempston picks the course, fire starts; the keys push the marble; the
   original's autopilot (B) finishes course 1 (a score); on course 2 the
   marble is pushed off the edge (a fall, three seconds off) until the clock
   runs out: game over, the initials for the high-score table, back to the
   title. The frame rate is measured.
4. The recording replayed twice: every frame hash must match.
Screenshots go to screenshots/.
"""
import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / "tools" / "zxdev" / "src"))
sys.path.insert(0, str(HERE / "tools"))

from zxdev.emu import Emulator  # noqa: E402
import test_rsmath  # noqa: E402

NEX = HERE / "build" / "rolling_steel.nex"
SHOTS = HERE / "screenshots"
REC = HERE / "build" / "bot-game.json"
RULE_STEPS = 7926                        # the attract demos of courses 1-3 (and the title before course 4)
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


def perf(log):
    """(real frames, loops, steps) of each perf line"""
    return [tuple(int(v) for v in m) for m in re.findall(r"perf frames=\d+ real=(\d+) loops=(\d+) steps=(\d+)", log)]


SHOTS.mkdir(exist_ok=True)

print("the 32-bit maths")
with Emulator(machine="next") as e:
    e.load(NEX)
    e.hold("T")
    ok = until(e, "rs: mathtest", 2000)
    e.release()
    m = re.search(r"mathtest n=(\d+) h=([0-9A-F]+)", e.log(0))
    want = f"{test_rsmath.selftest(2000):08X}"
    check(f"rsmath.asm's products, divisions, shifts, roots = C's (hash {want})",
          ok and m is not None and m.group(2) == want, str(m and m.groups()))

print("the rules (the original's C on the host against the Next)")
(HERE / "build-trace").mkdir(exist_ok=True)
r = subprocess.run(["cc", "-O2", "-std=gnu89", "-w", "-include", "tools/long32.h", "-Itools/orig",
                    "-o", "build-trace/hostsim", "tools/hostsim.c", "tools/orig/game.c", "tools/orig/phys.c",
                    "tools/orig/course.c"], cwd=HERE, capture_output=True, text=True)
check("the original's rules build on the host", r.returncode == 0, r.stderr[-300:])
host = subprocess.run(["build-trace/hostsim", str(RULE_STEPS)], cwd=HERE, capture_output=True, text=True).stdout
want = re.findall(r"rs: hash step=(\d+) state=(\d+) level=(\d+) h=([0-9A-F]+)", host)
with Emulator(machine="next") as e:
    e.load(NEX)
    e.hold("H")
    until(e, "rs: ready", 300)
    e.release()
    last = want[-1]
    ok = until(e, f"rs: hash step={last[0]} ", 40000)
    got = re.findall(r"rs: hash step=(\d+) state=(\d+) level=(\d+) h=([0-9A-F]+)", e.log(0))
    print(f"  {len(want)} changes of state in {RULE_STEPS} steps (courses 1-3 by the autopilot)")
    check("the Next's race hashes = the original's, at every change of state",
          ok and got[:len(want)] == want, f"first difference: {next(((a, b) for a, b in zip(want, got) if a != b), None)}")

print("a game")
with Emulator(machine="next") as e:
    e.load(NEX)
    e.record_input("start", from_reset=True)
    check("boots to the title", until(e, "rs: state 0", 300))
    check("Kempston found", "kempston=1" in e.log(0))
    e.run(250)
    e.screenshot(SHOTS / "title.png")
    # Kempston: down picks course 2, up course 1 again, fire starts
    e.press("DOWN")
    e.press("UP")
    e.press("FIRE")
    check("Kempston fire starts course 1", until(e, "rs: state 1 level=1", 60))
    # the keys push the marble: Q (up) is away from the camera
    e.hold("Q")
    e.run(80)
    e.release()
    e.screenshot(SHOTS / "play.png")
    m = re.findall(r"run course=1 t=\d+ progress=(\d+)", e.log(0))
    e.press("B")
    check("B: the autopilot on", until(e, "rs: autopilot 1", 20))
    mark = len(e.log(0))
    e.run(400)
    e.screenshot(SHOTS / "autopilot.png")
    ok = until(e, "rs: score", 6000)
    m = re.search(r"rs: score (\d+) course=1 medal=(\d+)", e.log(0))
    check("course 1 done, scored", ok and m is not None and int(m.group(1)) > 1000, e.log(0)[-300:])
    fp = perf(e.log(0)[mark:])
    e.run(30)
    e.screenshot(SHOTS / "clear.png")
    check("on to course 2", until(e, "rs: state 1 level=2", 400))
    e.press("B")
    check("B: the autopilot off", until(e, "rs: autopilot 0", 20))
    # off the edge: the course runs up and right, so down and left is the void
    e.hold("A O")
    ok = until(e, "death FELL OFF", 1500)
    e.run(8)
    e.screenshot(SHOTS / "fall.png")
    e.release()
    check("pushed off the edge: a fall, three seconds off", ok)
    # nothing more but falls: the clock runs out
    e.hold("A O")
    ok = until(e, "rs: state 3", 30000)
    e.release()
    check("the clock runs out: game over", ok)
    e.run(80)
    e.screenshot(SHOTS / "gameover.png")
    e.press("SPACE")
    check("a high score: the initials", until(e, "rs: initials", 60))
    e.run(10)
    e.press("Z")
    e.press("E")
    e.screenshot(SHOTS / "initials.png")
    e.press("D")
    check("initials ZED in the table", until(e, "rs: hiscore ZED", 60))
    check("back to the title", until(e, "rs: state 0", 100))
    e.run(320)                               # the title shows the table in turn with the courses
    e.screenshot(SHOTS / "hiscores.png")
    rec = e.record_input("save", REC)
    print(f"  recorded {rec['frames']} frames, {rec['events']} input changes")

    real = sum(f[0] for f in fp)
    loops = sum(f[1] for f in fp)
    steps = sum(f[2] for f in fp)
    fps = loops * 50 / real if real else 0
    speed = steps / real if real else 0
    print(f"  the autopilot on course 1: {fps:.1f} frames a second, the rules at {50 * speed:.1f} steps a second "
          f"({100 * speed:.0f}% of the original's 50)")
    check("frames drawn and the rules run (a steady loop)", fps > 3 and speed > 0.4, f"{fps} {speed}")

seqs = []
for i in range(2):
    with Emulator(machine="next") as e:
        r = e.replay(REC)
        seqs.append([h for _, h in r["hashes"]])
        check(f"replay {i + 1}: every frame hash matches the recorded game", r["ok"],
              f"{r.get('mismatches')} mismatches, first at {r.get('first_mismatch')}")
check("two replays give identical screens", seqs[0] == seqs[1] and len(seqs[0]) > 3000)

print(f"{'FAIL' if failures else 'PASS'}: rolling_steel")
sys.exit(1 if failures else 0)
