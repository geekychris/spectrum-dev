"""Boing Twist (Next) - plays games through the harness.

1. The rules on the Z80N: with T held at start-up the game runs the
   original's self-test, bt_vector_hash() (scrambles of every size and a long
   random walk of moves and undos), which must give the original's
   tests/vectors.h.
2. A game, recorded from reset: the title (the Boing ball bouncing round
   its room, music on two TurboSound chips), the select screen (the Earth on
   Easy), the scramble; a bot plays the scramble backwards through the keys
   (the band with up/down and a twist with left/right, the half with Z / X
   or Kempston fire + left/right and a flip with SPACE), with an undo, a
   peek, the ball turned (L) and tilted (W, a flip seen from 36 degrees)
   along the way, until the picture is whole: solved, scored; the initials
   for the high-score table, back to the title with the table showing.
   The frame rate is measured while the moves animate.
3. Another game, given up (R): the ball solves itself move by move,
   "solved for you", no score.
4. The recording replayed twice: every frame hash must match.
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

NEX = HERE / "build" / "boing_twist.nex"
SHOTS = HERE / "screenshots"
REC = HERE / "build" / "game.json"
VECTOR = re.search(r"0x([0-9a-fA-F]{8})", (HERE / "tools" / "orig" / "tests" / "vectors.h").read_text()).group(1).upper()
failures = []

# BtPuzzle (bt_core.h) on the Z80: bands segs (2 each), slot[128] (2 each),
# sig[128][2] (4 each), hist[1024] (2 each), nhist nscramble moves
OFF_HIST, OFF_NHIST = 4 + 256 + 1024, 4 + 256 + 1024 + 2048


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


def until(e, text, frames):
    """run until a log line starts, then a frame more (the whole line)"""
    ok = e.run_until(text, frames)
    e.run(1)
    return ok


def u8(e, sym):
    return e.read_mem(sym, 1)[0]


def puzzle(e):
    raw = e.read_mem("_P", OFF_NHIST + 6)
    bands, segs = struct.unpack_from("<hh", raw, 0)
    nhist, nscr, moves = struct.unpack_from("<hhh", raw, OFF_NHIST)
    hist = list(struct.unpack_from(f"<{nhist}h", raw, OFF_HIST))
    return bands, segs, hist, nscr, moves


def perf(log):
    """the perf lines: (fps x 10 while moving, frames drawn, channel A's volume on chips 0, 1, 2)"""
    return [tuple(int(v) for v in m) for m in
            re.findall(r"perf fps10=(\d+) drawn=(\d+) .*?ay=(\d+),(\d+),(\d+)", log)]


def tap(e, key):
    e.press(key, hold_frames=2, release_frames=2)


def do_move(e, m, segs, kempston=False):
    """one move by the keys: choose the band or the half, then twist or flip"""
    if m & 0x100:
        k = m & 0xFF
        while u8(e, "_sel_half") != k:
            d = (k - u8(e, "_sel_half")) % segs
            if kempston:                         # fire held, left / right
                e.hold("FIRE")
                e.run(2)
                e.press("RIGHT" if d <= segs // 2 else "LEFT", hold_frames=2, release_frames=2)
                e.release()
                e.run(2)
            else:
                tap(e, "X" if d <= segs // 2 else "Z")
        tap(e, "SPACE")
    else:
        b = (m >> 1) & 0x7F
        while u8(e, "_sel_band") != b:
            tap(e, "DOWN" if u8(e, "_sel_band") < b else "UP")
        tap(e, "LEFT" if m & 1 else "RIGHT")
    return e.run_until("bt: move", 400)


SHOTS.mkdir(exist_ok=True)

print("the rules (the original's self-test)")
with Emulator(machine="next") as e:
    e.load(NEX)
    e.hold("T")
    ok = until(e, "bt: selftest", 30000)
    e.release()
    m = re.search(r"selftest hash=([0-9A-F]{8})", e.log(0))
    check(f"bt_vector_hash() on the Z80N = {VECTOR} (tests/vectors.h)", ok and m and m.group(1) == VECTOR,
          m.group(1) if m else e.log(0)[-200:])

print("a game")
fps = []
with Emulator(machine="next") as e:
    e.load(NEX)
    e.record_input("start", from_reset=True)
    check("boots to the title", until(e, "bt: screen TITLE", 300))
    check("Kempston found", "kempston=1" in e.log(0))
    e.run(300)
    e.screenshot(SHOTS / "title.png")
    p = perf(e.log(0))
    check("the music plays on two TurboSound chips", any(f[2] > 0 for f in p) and any(f[3] > 0 for f in p), str(p))
    check("the title's ball bounces (drawn every pass)", bool(p) and p[-1][1] > 30, str(p[-1:]))
    e.press("SPACE")
    check("fire: the select screen", until(e, "bt: screen SELECT", 50))
    tap(e, "RIGHT")                              # the picture row: the Boing ball -> the Earth
    e.run(40)
    e.screenshot(SHOTS / "select.png")
    e.press("ENTER")
    ok = until(e, "bt: play pic=1 level=0", 50)
    check("ENTER plays the Earth on Easy (4 x 8)", ok and "grid=4x8 scramble=8" in e.log(0))
    e.run(30)
    e.screenshot(SHOTS / "scrambling.png")
    check("the scramble plays move by move", until(e, "bt: scrambled n=8", 600))
    bands, segs, hist, nscr, moves = puzzle(e)
    check("8 scramble moves in the puzzle's history", nscr == 8 and len(hist) == 8 and moves == 0, str(hist))
    e.run(5)
    e.screenshot(SHOTS / "play.png")
    # a move and its undo first
    mark = len(e.log(0))
    tap(e, "RIGHT")
    e.run(4)
    e.screenshot(SHOTS / "twist.png")
    ok = e.run_until("bt: move m=", 100)
    tap(e, "U")
    ok2 = e.run_until("bt: move m=", 100)
    e.run(3)
    _, _, h2, _, mv = puzzle(e)
    check("a twist, then U undoes it", ok and ok2 and h2 == hist and mv == 0, f"{h2} moves={mv}")
    tap(e, "L")                                  # turn the ball to look
    e.run(20)
    e.hold("H")                                  # peek: the picture as it should be
    e.run(10)
    e.screenshot(SHOTS / "peek.png")
    e.release()
    e.run(5)
    # the scramble backwards
    inverse = [m if m & 0x100 else m ^ 1 for m in reversed(hist)]
    solved = False
    for i, m in enumerate(inverse):
        if i == 2:                               # the next flip seen from 36 degrees up
            for _ in range(2):
                tap(e, "W")
        if m & 0x100 and i >= 2 and not any(x & 0x100 for x in inverse[2:i]):
            # mid-flip from above
            k = m & 0xFF
            while u8(e, "_sel_half") != k:
                d = (k - u8(e, "_sel_half")) % segs
                tap(e, "X" if d <= segs // 2 else "Z")
            tap(e, "SPACE")
            e.run(4)
            e.screenshot(SHOTS / "flip.png")
            ok = e.run_until("bt: move", 400)
        else:
            ok = do_move(e, m, segs, kempston=(i == 1))
        if not ok:
            check(f"move {i + 1} ({m:#x}) by the keys", False, e.log(0)[-300:])
            break
        e.run(3)
        if "bt: solved" in e.log(0)[mark:]:
            solved = True
            break
    fp = [f[0] / 10 for f in perf(e.log(0)[mark:]) if f[1] >= 10]
    fps = fp
    solved = solved or e.run_until("bt: solved", 100)
    e.run(2)
    m = re.search(r"solved moves=(\d+) secs=(\d+) peeks=(\d+) score=(\d+) gave_up=0", e.log(0))
    check("the bot puts the picture back together: solved, scored", solved and m is not None and int(m.group(4)) > 0,
          e.log(0)[-300:])
    check("the peek was counted", m is not None and m.group(3) == "1")
    check("the moves' sounds on the third TurboSound chip", any(f[4] > 0 for f in perf(e.log(0)[mark:])))
    check("the solved screen", until(e, "bt: screen SOLVED", 20) or "screen SOLVED" in e.log(0))
    e.run(60)
    e.screenshot(SHOTS / "solved.png")
    e.press("SPACE")
    check("a high score: the initials", until(e, "bt: screen ENTRY", 50))
    e.run(10)
    e.press("Z", hold_frames=4, release_frames=4)
    e.press("E", hold_frames=4, release_frames=4)
    e.screenshot(SHOTS / "initials.png")
    e.press("D", hold_frames=4, release_frames=4)
    check("initials ZED in the table", until(e, "bt: hiscore ZED", 50))
    check("back to the title", until(e, "bt: screen TITLE", 50))
    e.run(40)
    e.screenshot(SHOTS / "hiscores.png")

    # another game, given up: the ball solves itself
    e.press("SPACE")
    until(e, "bt: screen SELECT", 50)
    tap(e, "UP")
    tap(e, "RIGHT")                              # Medium
    e.run(30)
    e.press("ENTER")
    ok = until(e, "bt: play", 50) and "level=1 grid=6x12 scramble=20" in e.log(0)
    check("Medium: a 6 x 12 ball scrambled 20 moves", ok, e.log(0)[-200:])
    check("scrambled", until(e, "bt: scrambled n=20", 1500))
    e.run(10)
    e.screenshot(SHOTS / "medium.png")
    tap(e, "R")
    check("R: give up", until(e, "bt: give up", 20))
    ok = until(e, "bt: solved", 1500)
    check("it solves itself: solved for you, no score", ok and re.search(r"score=0 gave_up=1", e.log(0)) is not None,
          e.log(0)[-200:])
    e.run(60)
    e.press("SPACE")
    check("back to the select screen", until(e, "bt: screen SELECT", 50))
    rec = e.record_input("save", REC)
    print(f"  recorded {rec['frames']} frames, {rec['events']} input changes")
    print(f"  pictures a second while the moves animate: {fps}")
    check("at least 12 pictures a second while moving", bool(fps) and min(fps) >= 12, str(fps))

seqs = []
for i in range(2):
    with Emulator(machine="next") as e:
        r = e.replay(REC)
        seqs.append([h for _, h in r["hashes"]])
        check(f"replay {i + 1}: every frame hash matches the recorded game", r["ok"],
              f"{r.get('mismatches')} mismatches, first at {r.get('first_mismatch')}")
check("two replays give identical screens", seqs[0] == seqs[1] and len(seqs[0]) > 1000)

print(f"{'FAIL' if failures else 'PASS'}: boing_twist")
sys.exit(1 if failures else 0)
