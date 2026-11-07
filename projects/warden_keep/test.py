"""Warden Keep (Next) - plays two games through the harness, recorded from reset.

Game 1: Kempston fire starts; the route pushes a barrel (3,5 -> 3,4), takes
the jewel at 2,6 with a Kempston step, holds P (the original's auto-repeat:
a step at once, then after 12 frames and every 6) to the jewel at 6,6, and
steps up to the last one at 6,3: the room won, 300 + the time bonus; the
initials ACE go in the table.
Game 2: one jewel, then nothing: the clock runs out (time up, game over);
the initials BOB, back to the title.
Game 3: R picks the relaxed game (the original's, no clock): 64 seconds
pass with no time up; BREAK goes back to the title.
Also: the music on two TurboSound chips, the frame rate, the replay of the
whole session twice (every frame hash must match). Screenshots go to
screenshots/.
"""
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / "tools" / "zxdev" / "src"))

from zxdev.emu import Emulator  # noqa: E402

NEX = HERE / "build" / "warden_keep.nex"
SHOTS = HERE / "screenshots"
REC = HERE / "build" / "bot-game.json"
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


def steps(e, keys):
    for k in keys:
        e.press(k)
        e.run(4)


def perf(log):
    return [tuple(int(v) for v in m) for m in re.findall(r"perf loops=(\d+) frames=(\d+) ay=(\d+),(\d+),(\d+),(\d+)", log)]


SHOTS.mkdir(exist_ok=True)
with Emulator(machine="next") as e:
    e.load(NEX)
    e.record_input("start", from_reset=True)
    check("boots to the title", until(e, "wk: state TITLE", 300))
    check("Kempston found", "kempston=1" in e.log(0))
    e.run(120)
    e.screenshot(SHOTS / "title.png")
    p = perf(e.log(0))
    check("the music plays on two TurboSound chips", bool(p) and (p[-1][2] & 7) != 7 and (p[-1][4] & 7) != 7
          and p[-1][3] + p[-1][5] > 0, str(p[-1:]))

    # game 1
    e.press("FIRE")
    check("Kempston fire starts the room", until(e, "wk: state PLAYING score=0 jewels=3 time=60", 30))
    mark = len(e.log(0))
    steps(e, ["Q"])
    check("a barrel pushed (3,5 -> 3,4)", "wk: push" in e.log(0)[mark:] and "wk: player 3 5" in e.log(0)[mark:])
    e.screenshot(SHOTS / "play.png")
    steps(e, ["A"])
    e.press("LEFT")
    e.run(4)
    check("a Kempston step onto the first jewel", "wk: player 2 6" in e.log(0) and "wk: jewel score=100 left=2" in e.log(0))
    mark = len(e.log(0))
    e.hold("P")
    e.run(27)                            # steps at once, then after 12, 18 and 24 frames
    e.release()
    e.run(4)
    moved = re.findall(r"wk: player (\d) (\d)", e.log(0)[mark:])
    check("P held: the original's auto-repeat, four steps in 27 frames", moved == [("3", "6"), ("4", "6"), ("5", "6"), ("6", "6")],
          str(moved))
    check("the second jewel", "wk: jewel score=200 left=1" in e.log(0))
    steps(e, ["Q", "Q", "Q"])
    ok = until(e, "wk: state WIN", 20)
    m = re.search(r"wk: state WIN score=(\d+)", e.log(0))
    score1 = int(m.group(1)) if m else 0
    check("the room won: 300 and a time bonus", ok and 300 < score1 <= 890, str(score1))
    e.run(30)
    e.screenshot(SHOTS / "win.png")
    e.run(200)
    e.press("SPACE")
    e.run(10)
    e.press("A")
    e.press("C")
    e.screenshot(SHOTS / "initials.png")
    e.press("E")
    check("initials ACE in the table", until(e, f"wk: hiscore ACE {score1} place=1", 40))
    check("back to the title", "wk: state TITLE" in e.log(0)[-200:])
    e.run(20)
    e.screenshot(SHOTS / "hiscores.png")

    # game 2: a jewel, then the clock runs out
    e.run(40)
    e.press("SPACE")
    check("a second room", until(e, "wk: state PLAYING score=0", 30))
    steps(e, ["O"])
    check("one jewel", "wk: jewel score=100 left=2" in e.log(0)[-400:])
    mark = len(e.log(0))
    e.run(2400)
    e.screenshot(SHOTS / "clock.png")
    pp = perf(e.log(0)[mark:])
    ok = until(e, "wk: state OVER", 800)
    check("the clock runs out: time up, game over", ok and "wk: state OVER score=100 jewels=2 time=0" in e.log(0),
          e.log(0)[-200:])
    e.run(30)
    e.screenshot(SHOTS / "gameover.png")
    e.run(200)
    e.press("SPACE")
    e.run(10)
    for k in "BOB":
        e.press(k)
    check("initials BOB in the table", until(e, "wk: hiscore BOB 100", 40))
    check("back to the title again", "wk: state TITLE" in e.log(0)[-200:])

    # game 3: relaxed, the original's untimed game
    e.run(20)
    e.press("R")
    check("R: the relaxed game (the original, no clock)", until(e, "wk: mode relaxed", 20))
    e.press("SPACE")
    check("a relaxed room", until(e, "wk: state PLAYING score=0 jewels=3 time=60 relaxed=1", 30))
    steps(e, ["O"])
    mark = len(e.log(0))
    e.run(3200)                          # past the timed game's 60 s
    e.screenshot(SHOTS / "relaxed.png")
    check("no clock: still playing after 64 s", "wk: state" not in e.log(0)[mark:], e.log(0)[mark:][-200:])
    e.press("CAPS+SPACE")
    check("BREAK: back to the title", until(e, "wk: state TITLE", 20))
    rec = e.record_input("save", REC)
    print(f"  recorded {rec['frames']} frames, {rec['events']} input changes")
    loops = sum(x[0] for x in pp)
    frames = sum(x[1] for x in pp)
    fps = loops * 50 / frames if frames else 0
    print(f"  playing: {fps:.1f} frames a second")
    check("50 frames a second in play", fps >= 49, str(fps))

seqs = []
for i in range(2):
    with Emulator(machine="next") as e:
        r = e.replay(REC)
        seqs.append([h for _, h in r["hashes"]])
        check(f"replay {i + 1}: every frame hash matches the recorded game", r["ok"],
              f"{r.get('mismatches')} mismatches, first at {r.get('first_mismatch')}")
check("two replays give identical screens", seqs[0] == seqs[1] and len(seqs[0]) > 3000)

print(f"{'FAIL' if failures else 'PASS'}: warden_keep")
sys.exit(1 if failures else 0)
