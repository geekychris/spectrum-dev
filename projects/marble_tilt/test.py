"""Marble Tilt (Next) - plays a game through the harness.

1. The rules on the Z80N: with T held at start-up the game runs the
   original's whole autopilot game (its tests/test_cases.c) and logs the
   state hash, which must be the original's tests/vectors.h.
2. A game, recorded from reset: the title (music on two TurboSound chips,
   Kempston picks the level), the board drawn on the Next = the host's
   projection (tools/gen_view.py, to a few edge pixels), the keys tilt the board, the original's
   autopilot (B) rolls through level 1 (an exit, a score), then with the
   autopilot off the marble is rolled into a hole on The Moat (a fall,
   three seconds off) and the clock runs out: game over, the initials for
   the high-score table, back to the title. The frame rate is measured.
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
sys.path.insert(0, str(HERE / "tools"))

from zxdev.emu import Emulator  # noqa: E402

NEX = HERE / "build" / "marble_tilt.nex"
SHOTS = HERE / "screenshots"
REC = HERE / "build" / "bot-game.json"
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


def game_fix(e, field):
    """MTGame's 16.16 fields (mt.h): x z vx vz roll pitch"""
    off = 4 + 8 + 12 * 13 + 4 * ["x", "z", "vx", "vz", "roll", "pitch"].index(field)
    return struct.unpack("<l", e.read_mem("_game", off + 4)[off:])[0]


def perf(log):
    """the perf lines: (loops, ticks, frames, ay mix0, vol0, mix1, vol1)"""
    return [tuple(int(v) for v in m) for m in
            re.findall(r"perf loops=(\d+) ticks=(\d+) frames=(\d+) .*?ay=(\d+),(\d+),(\d+),(\d+)", log)]


SHOTS.mkdir(exist_ok=True)

print("the rules (the original's whole autopilot game)")
with Emulator(machine="next") as e:
    e.load(NEX)
    e.hold("T")
    ok = until(e, "mt: selftest", 3000)
    e.release()
    m = re.search(r"selftest ticks=(\d+) level=(\d+) won=(\d+) score=(\d+) hash=([0-9A-F]+)", e.log(0))
    check("self-test ran", ok and m is not None)
    if m:
        check(f"5000 ticks, all 8 levels, score 9360, hash {VECTOR} (tests/vectors.h)",
              m.groups() == ("5000", "8", "1", "9360", VECTOR), str(m.groups()))

print("a game")
with Emulator(machine="next") as e:
    e.load(NEX)
    e.record_input("start", from_reset=True)
    check("boots to the title", until(e, "mt: state TITLE", 300))
    check("Kempston found", "kempston=1" in e.log(0))
    e.run(300)
    e.screenshot(SHOTS / "title.png")
    p = perf(e.log(0))
    check("the music plays on two TurboSound chips (tones on, volumes up)",
          bool(p) and (p[-1][3] & 7) != 7 and (p[-1][5] & 7) != 7 and p[-1][4] + p[-1][6] > 0, str(p[-1:]))
    # Kempston: right picks level 2, left level 1 again, fire starts
    e.press("RIGHT")
    e.press("LEFT")
    e.press("RIGHT")
    e.press("LEFT")
    e.press("FIRE")
    check("Kempston fire starts level 1", e.run_until("mt: start level 1 FIRST ROLL", 50))
    # the board at rest, drawn on the Next = the host's projection of the same polygons
    e.run(80)
    import gen_view
    want = gen_view.render(0, 0, 0)
    got = e.screenshot(layer="layer2").convert("RGB")
    diff = sum(1 for y in range(192) for x in range(256) if got.getpixel((x, y)) != want.getpixel((x, y)))
    if diff:
        got.save(HERE / "build" / "l2_start.png")
        want.save(HERE / "build" / "l2_start_host.png")
    # the model rounds a few edge pixels differently from l2poly (31 at this board)
    check("the board on Layer 2 = the host's projection (all but < 0.2% of pixels at edges)", diff < 100,
          f"{diff} pixels differ (build/l2_start*.png)")
    e.screenshot(SHOTS / "start.png")
    # the keys tilt it: P (right) rolls it right, the board follows
    e.hold("P")
    e.run(30)
    roll = game_fix(e, "roll")
    e.release()
    check("P tilts the board right (the tilt at its most)", roll == 7864, str(roll))
    shown = (e.read_mem("_view_roll", 1)[0], e.read_mem("_view_pitch", 1)[0])
    check("the board drawn tilted right follows", shown == (10, 5), str(shown))
    e.hold("Q")
    e.run(30)
    pitch = game_fix(e, "pitch")
    e.release()
    check("Q tilts it away", pitch == 7864, str(pitch))
    # the autopilot through level 1
    e.press("B")
    check("B: the autopilot on", e.run_until("mt: autoplay 1", 20))
    mark = len(e.log(0))
    e.run(300)
    e.screenshot(SHOTS / "play.png")
    ok = until(e, "mt: ev EXIT", 3000)
    m = re.search(r"ev EXIT a=1 b=(\d+) score=(\d+)", e.log(0))
    check("level 1 done, scored", ok and m is not None and int(m.group(2)) > 100, e.log(0)[-300:])
    fp = perf(e.log(0)[mark:])
    e.run(15)
    e.screenshot(SHOTS / "through.png")
    check("on to The Moat", e.run_until("mt: ev LEVEL a=2", 300))
    e.press("B")
    check("B: the autopilot off", e.run_until("mt: autoplay 0", 20))
    # into the moat: down and right from the start is a hole
    e.run(30)
    e.hold("A P")
    ok = e.run_until("mt: ev FALL", 300)
    e.release()
    e.run(6)
    e.screenshot(SHOTS / "fall.png")
    check("rolled into a hole: a fall, back to the start", ok and e.run_until("mt: ev RESPAWN", 100))
    # nothing more: the clock runs out
    ok = e.run_until("mt: state OVER", 4000)
    check("the clock runs out: game over", ok and "mt: ev TIMEOUT" in e.log(0))
    e.run(100)
    e.screenshot(SHOTS / "gameover.png")
    e.press("SPACE")
    check("a high score: the initials", e.run_until("mt: state ENTRY", 50))
    e.run(10)
    e.press("Z")
    e.press("E")
    e.screenshot(SHOTS / "initials.png")
    e.press("D")
    check("initials ZED in the table", e.run_until("mt: hiscore ZED", 50))
    check("back to the title", e.run_until("mt: state TITLE", 50))
    e.run(30)
    e.screenshot(SHOTS / "hiscores.png")
    over = struct.unpack("<H", e.read_mem("_frame_overruns", 2))[0]
    rec = e.record_input("save", REC)
    print(f"  recorded {rec['frames']} frames, {rec['events']} input changes")

    # the frame rate while the autopilot played level 1
    loops = sum(f[0] for f in fp)
    ticks = sum(f[1] for f in fp)
    frames = sum(f[2] for f in fp)
    fps = loops * 50 / frames if frames else 0
    tps = ticks * 50 / frames if frames else 0
    print(f"  playing: {fps:.1f} passes a second (marble, text, gauge), {tps:.1f} ticks a second, "
          f"{over} frames overrun in the whole game")
    check("50 frames a second, the rules at 25 ticks a second", fps >= 47 and abs(tps - 25) < 0.6, f"{fps} {tps}")

seqs = []
for i in range(2):
    with Emulator(machine="next") as e:
        r = e.replay(REC)
        seqs.append([h for _, h in r["hashes"]])
        check(f"replay {i + 1}: every frame hash matches the recorded game", r["ok"],
              f"{r.get('mismatches')} mismatches, first at {r.get('first_mismatch')}")
check("two replays give identical screens", seqs[0] == seqs[1] and len(seqs[0]) > 3000)

print(f"{'FAIL' if failures else 'PASS'}: marble_tilt")
sys.exit(1 if failures else 0)
