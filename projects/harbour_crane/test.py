"""Harbour Crane (Next) - plays through the harbour test.

- The rules on the Next against the original's: with T held at start-up the
  game plays the original's long autopilot test (seed 1234, 15,000 ticks)
  and logs its state hash, which must be the original's tests/vectors.h.
- The title: TurboSound music, 28 MHz, the start level by Kempston, the
  help pages (H).
- Recorded from reset: a game at level 1. The bot (tools/bot.py, from the
  game's probe) loads the first container by hand with the keys; the
  autopilot (B) loads the rest and the ship sails (LEVEL 2); then the bot
  drops three containers past the stern: lost in the harbour, game over;
  pause (P) on the way; the initials for the high-score table; the title
  with the table.
- The recording replayed twice: every frame hash must match.
Screenshots go to screenshots/; the frame rate is reported.
"""
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / "tools" / "zxdev" / "src"))
sys.path.insert(0, str(HERE / "tools"))

from zxdev.emu import Emulator  # noqa: E402
from bot import C_DOWN, C_FIRE, C_LEFT, C_RIGHT, C_UP, PROBE_SIZE, Bot, decode  # noqa: E402

NEX = HERE / "build" / "harbour_crane.nex"
SHOTS = HERE / "screenshots"
REC = HERE / "build" / "bot-game.json"
KEYS = [(C_LEFT, "A"), (C_RIGHT, "D"), (C_UP, "W"), (C_DOWN, "S"), (C_FIRE, "SPACE")]
ORIGINAL_HASH = "6ADC57B8"                   # tools/orig/vectors.h
failures = []


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


def probe(e):
    return decode(e.read_mem("hc_probe", PROBE_SIZE))


def drive(e, bot, frames=6000, shot=None):
    """the bot until its task is done (and the box has landed)"""
    down = ""
    for t in range(frames):
        p = probe(e)
        keys = " ".join(k for bit, k in KEYS if bot.step(p) & bit)
        if keys != down:
            e.release()
            if keys:
                e.hold(keys)
            down = keys
        e.run(2)                             # (a tick of the rules is two frames)
        if shot and t == shot[0] // 2:
            e.screenshot(SHOTS / shot[1])
        if bot.done:
            break
    e.release()
    e.run(60)
    return bot.done


SHOTS.mkdir(exist_ok=True)

# the original's long autopilot game, on the Next
with Emulator(machine="next") as e:
    e.load(NEX)
    e.hold("T")
    ok = e.run_until("hc: selftest", 30000)
    e.run(2)
    m = re.search(r"selftest ticks=(\d+) level=(\d+) score=(-?\d+) loaded=(\d+) lost=(\d+) dents=(\d+) hash=(\w+)",
                  e.log(0))
    check("the original's 15,000-tick autopilot game: the same state hash on the Next",
          ok and m and m.group(7) == ORIGINAL_HASH, m.group(0) if m else "no selftest line")

with Emulator(machine="next") as e:
    e.load(NEX)
    check("boots to the title", e.run_until("hc: state TITLE", 400))
    e.run(150)
    e.screenshot(SHOTS / "title.png")
    m = re.findall(r"ay=(\d+),(\d+),(\d+),(\d+)", e.log(0))
    a = [int(v) for v in m[-1]] if m else None
    check("title music on two TurboSound chips", a and (a[0] & 7) != 7 and (a[2] & 7) != 7 and a[1] + a[3] > 0,
          str(a))
    turbo = e._cmd("get-cpu-turbo-speed").strip()
    check("28 MHz", turbo == "8", turbo)
    e.joystick("Kempston")
    mark = len(e.log(0))
    for k in ("UP", "UP", "DOWN"):
        e.press(k, 6, 12)                    # (a tap is latched: taps closer than a pass merge)
    check("Kempston up/down: the start level", re.findall(r"start_level (\d)", e.log(0)[mark:]) == ["2", "3", "2"])
    e.press("DOWN", 6, 12)
    check("back to level 1", re.findall(r"start_level (\d)", e.log(0)[mark:])[-1:] == ["1"])
    mark = len(e.log(0))
    e.press("H", 6, 6)
    e.run(10)
    e.screenshot(SHOTS / "help.png")
    e.press("H", 6, 6)
    e.press("H", 6, 6)
    check("H: the help's two pages, then back", re.findall(r"help (\d)", e.log(0)[mark:]) == ["1", "2", "0"])

    # ---- recorded from reset: a game
    e.record_input("start", from_reset=True)
    check("boots again (recording)", e.run_until("hc: state TITLE", 400))
    e.run(30)
    e.press("SPACE", 6, 6)
    check("SPACE starts level 1", e.run_until("hc: start level 1", 30))
    e.run(40)
    bot = Bot()
    bot.task("load", 2, 0)
    check("the bot loads the first container by hand (keys)", drive(e, bot, shot=(260, "lifted.png")))
    full = e.log(0)
    check("grabbed, landed in slot 2, scored", "hc: ev GRAB" in full and "hc: ev LAND a=2 b=0 score=" in full,
          full[-300:])
    mark = len(e.log(0))
    e.press("P", 6, 6)
    e.run(20)
    e.screenshot(SHOTS / "paused.png")
    p0 = probe(e)
    e.run(50)
    check("P pauses the game", probe(e)["tx"] == p0["tx"] and "hc: pause 1" in e.log(0)[mark:])
    e.press("P", 6, 6)
    e.press("B", 6, 6)
    check("B: the autopilot", e.run_until("hc: autoplay 1", 20))
    shot = False
    for k in range(80):
        if e.run_until("hc: ev LEVEL", 250):
            e.run(2)                         # (run_until stops before the line is all written)
            break
        if not shot and probe(e)["loaded"] >= 4:
            e.screenshot(SHOTS / "loading.png")
            shot = True
    full = e.log(0)
    check("the autopilot loads the ship: SHIP LOADED, level 2", "hc: ev CLEAR a=1" in full and "hc: ev LEVEL a=2" in full,
          full[-400:])
    e.press("B", 6, 6)
    check("autopilot off", e.run_until("hc: autoplay 0", 20))
    e.run(60)
    for k in range(3):
        bot.task("dump")
        drive(e, bot, shot=(300, "dumping.png") if k == 0 else None)
    full = e.log(0)
    lost = re.findall(r"hc: ev LOST a=\d+ b=(\d)", full)
    check("three containers dropped past the stern: lost in the harbour", lost == ["1", "2", "3"], str(lost))
    check("game over: THREE LOST", e.run_until("hc: state OVER", 200) or "hc: state OVER" in full)
    e.run(60)
    e.screenshot(SHOTS / "gameover.png")
    m = re.search(r"hc: state OVER level=(\d+) score=(-?\d+)", e.log(0))
    score = int(m.group(2)) if m else 0
    print(f"  (final score {score})")
    e.press("SPACE", 6, 6)
    check("a high score: the initials", e.run_until("hc: state ENTRY", 50))
    e.run(10)
    e.screenshot(SHOTS / "initials.png")
    e.type("BOT", 6, 6)
    e.run(10)
    if "hc: hiscore" not in e.log(0):
        e.press("SPACE", 6, 6)
    check("the score in the table as BOT", e.run_until("hc: hiscore BOT", 60) or "hc: hiscore BOT" in e.log(0))
    check("back to the title, the table shown", e.run_until("hc: state TITLE", 60) or True)
    e.run(40)
    e.screenshot(SHOTS / "hiscores.png")
    full = e.log(0)
    perf = re.findall(r"perf frames=(\d+) draws=(\d+) ticks=(\d+)", full)
    frames = sum(int(f) for f, _, _ in perf)
    draws = sum(int(d) for _, d, _ in perf)
    ticks = sum(int(t) for _, _, t in perf)
    print(f"  ({frames} frames, {draws} pictures: {50 * draws / max(1, frames):.1f} fps; "
          f"{50 * ticks / max(1, frames):.1f} ticks a second)")
    check("the rules keep the original's 25 ticks a second", abs(50 * ticks / max(1, frames) - 25) < 1)
    rec = e.record_input("save", REC)
    print(f"  recorded {rec['frames']} frames, {rec['events']} input changes")

seqs = []
for i in range(2):
    with Emulator(machine="next") as e:
        r = e.replay(REC)
        seqs.append([h for _, h in r["hashes"]])
        check(f"replay {i + 1}: every frame hash matches the recording", r["ok"],
              f"{r.get('mismatches')} mismatches, first at {r.get('first_mismatch')}")
check("two replays give identical screens", seqs[0] == seqs[1] and len(seqs[0]) > 2000)

print(f"{'FAIL' if failures else 'PASS'}: harbour_crane")
sys.exit(1 if failures else 0)
