"""Tank Duel (Spectrum Next) - plays a whole game through the harness.

1. The rules: the trace build (make TRACE=1) fights the original's 9000-tick
   autopilot fight (seed 1234) and must end in the original's state hash.
2. A game: boot -> the title (the autopilot fights behind it) -> start ->
   the game's own autopilot (B, as the original) fights: kills, score ->
   autopilot off: the enemies destroy the idle tank -> game over -> the
   initials (BOT) -> the title with BOT in the table. Help pages, pause
   and the Kempston on the way. The game is recorded from reset and
   replayed twice: every frame hash must match.
Screenshots go to projects/tank_duel/screenshots/.
"""
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "zxdev" / "src"))

from zxdev.emu import Emulator  # noqa: E402

HERE = Path(__file__).resolve().parent
NEX = HERE / "build" / "tank_duel.nex"
TRACE_NEX = HERE / "build-trace" / "tank_duel.nex"
SHOTS = HERE / "screenshots"
REC = HERE / "build" / "bot-game.json"
ORIGINAL_HASH = "7cfab335"             # amiga_mcp/examples/tank_duel/tests/vectors.h
failures = []


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


# 1. the rules, bit for bit
subprocess.run([str(ROOT / "bin" / "zx-make"), "-C", str(HERE), "TRACE=1"], check=True,
               stdout=subprocess.DEVNULL)
with Emulator(machine="next") as e:
    e.load(TRACE_NEX)
    check("trace: the long autopilot fight finishes", e.run_until("td: trace", 60000))
    e.run(1)
    m = re.search(r"td: trace round=(\d+) score=(\d+) kills=(\d+) hp=(\d+) frames=\d+ hash=(\w+)", e.log(0))
    check("trace: the original's result (round 2, 1300 points, 4 kills, 40 armour)",
          m and m.groups()[:4] == ("2", "1300", "4", "40"), m.group(0) if m else "")
    check("trace: the original's state hash", m and m.group(5) == ORIGINAL_HASH, m.group(5) if m else "")


def wait(e, text, frames):
    return e.run_until(text, frames)


# 2. a game
with Emulator(machine="next") as e:
    e.load(NEX)
    check("boots to the title", e.run_until("td: ready", 3000))
    e.record_input("start", from_reset=True)
    e.run_until("td: ready", 3000)
    e.run(150)
    e.screenshot(SHOTS / "title.png")
    title = e.screenshot()
    check("title drawn (3D demo behind the text)", len(title.getcolors(1 << 16)) >= 12)
    e.press("H")
    e.run(20)
    e.screenshot(SHOTS / "help.png")
    e.press("H")
    e.run(10)
    e.press("H")
    e.run(10)
    e.press("SPACE")
    check("FIRE starts round 1", e.run_until("td: start round=1 enemies=2", 200))
    e.run(60)
    e.screenshot(SHOTS / "round1.png")
    e.press("S")
    e.run(30)
    log = e.log()
    e.screenshot(SHOTS / "paused.png")
    e.press("S")
    e.press("B")
    check("B: the autopilot takes over", e.run_until("td: autoplay=1", 100))
    log = ""
    shots = set()
    for _ in range(80):                     # up to ~9600 frames
        e.run(120)
        log += e.log()
        if "kill" in log and "fight" not in shots:
            e.run(10)
            e.screenshot(SHOTS / "fight.png")
            shots.add("fight")
        if "td: clear" in log and "clear" not in shots:
            e.run(20)
            e.screenshot(SHOTS / "clear.png")
            shots.add("clear")
        scores = [int(s) for s in re.findall(r"td: \w+ score=(\d+)", log)]
        if "td: round score" in log and scores and max(scores) >= 600:
            break
    kills = log.count("td: kill")
    scores = [int(s) for s in re.findall(r"td: \w+ score=(\d+)", log)]
    check("the autopilot destroys enemies", kills >= 2, str(kills))
    check("and clears round 1", "td: clear" in log and "td: round score" in log)
    check("scores (kills + round bonus)", scores and max(scores) >= 600, str(scores[-3:]))
    fps = [int(f) for f in re.findall(r"td: fps=(\d+)", log)]
    ay = re.findall(r"ay=(\d+),(\d+),(\d+)", log)
    print(f"    fps {min(fps or [0])}..{max(fps or [0])}, mean {sum(fps) / max(1, len(fps)):.1f}")
    check("TurboSound effects play", any(sum(map(int, a)) > 0 for a in ay), str(ay[-4:]))
    # autopilot off: the enemies destroy the idle tank
    e.press("B")
    check("B again: the autopilot off", e.run_until("td: autoplay=0", 100))
    log = ""
    for _ in range(100):
        e.run(120)
        log += e.log()
        if "td: gameover" in log:
            break
    check("hit while idle", "td: hurt" in log)
    check("game over", "td: gameover" in log)
    m = re.search(r"td: gameover score=(\d+)", log)
    final = int(m.group(1)) if m else 0
    e.run(60)
    e.screenshot(SHOTS / "gameover.png")
    e.run(100)                              # the original's 40 ticks before FIRE counts
    e.press("SPACE")
    check("the score makes the high-score table (defaults 2500..500)", e.run_until("td: hiscore rank", 200), str(final))
    e.run(5)
    e.screenshot(SHOTS / "initials.png")
    e.type("BOT")
    check("initials typed", e.run_until("td: name=BOT", 200))
    check("back to the title", e.run_until("td: title", 100) or "td: title" in e.log(0)[-300:])
    e.run(100)
    e.screenshot(SHOTS / "hiscores.png")
    # the Kempston: fire starts, left turns
    e.joystick("Kempston")
    e.press("FIRE")
    check("Kempston fire starts a game", e.run_until("td: start", 200))
    yaw0 = e.read_mem("_game", 64)
    e.hold("LEFT")
    e.run(60)
    e.release()
    check("Kempston left turns the tank", e.read_mem("_game", 64) != yaw0)
    rec = e.record_input("save", REC)
    print(f"  recorded {rec['frames']} frames, {rec['events']} input changes")

seqs = []
for i in range(2):
    with Emulator(machine="next") as e:
        r = e.replay(REC)
        seqs.append([h for _, h in r["hashes"]])
        check(f"replay {i + 1}: every frame hash matches the recorded game", r["ok"],
              f"{r.get('mismatches')} mismatches, first at {r.get('first_mismatch')}")
check("two replays give identical screens", seqs[0] == seqs[1] and len(seqs[0]) > 1000)

print(f"{'FAIL' if failures else 'PASS'}: tank_duel")
sys.exit(1 if failures else 0)
