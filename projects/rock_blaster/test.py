"""Rock Blaster (48K) - plays a whole game through the harness.

Boot -> title -> start -> a bot aims at the nearest rock and fires (scores),
later flies into the rocks (loses lives) -> game over -> high-score entry
-> back to the title. The bot's game is recorded and replayed twice: every
frame hash must match. Then a 128K: the AY plays the original's music, and
the Kempston starts and steers a game. Screenshots go to
projects/rock_blaster/screenshots/.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "zxdev" / "src"))

from zxdev.emu import Emulator  # noqa: E402

HERE = Path(__file__).resolve().parent
TAP = HERE / "build" / "rock_blaster.tap"
SHOTS = HERE / "screenshots"
REC = HERE / "build" / "bot-game.json"
failures = []

sys.path.insert(0, str(Path(__file__).resolve().parent / "tools"))
from bot import state, step  # noqa: E402


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


def bot_game(e, shots=True):
    """Aim at the nearest rock and keep firing; after a while also thrust at
    it, so the ship gets hit and the game ends."""
    log, t = "", 0
    while "state=gameover" not in log and t < 20000:
        step(e, suicide=t > 2500)
        t += 4
        log += e.log()
        if shots and 600 <= t < 604:
            e.screenshot(SHOTS / "play.png")
    e.release()
    return log


with Emulator() as e:
    e.load(TAP)
    check("boots to the title", e.run_until("rb: ready", 4000))
    e.run(60)
    e.screenshot(SHOTS / "title.png")
    title = e.screenshot()
    check("title screen drawn", len(title.getcolors(1 << 16)) >= 5)
    e.record_input("start", from_reset=True)       # the whole run, for the replay check
    e.run_until("rb: ready", 4000)
    e.run(30)
    e.press("SPACE")
    check("FIRE starts the game", e.run_until("rb: state=playing", 100))
    log = bot_game(e)
    scores = [int(s) for s in re.findall(r"rb: score=(\d+)", log)]
    lives = [int(s) for s in re.findall(r"rb: lives=(\d+)", log)]
    levels = [int(s) for s in re.findall(r"rb: level=(\d+)", log)]
    fps = [int(s) for s in re.findall(r"rb: fps=(\d+)", log)]
    check("the bot shoots rocks and scores", scores and max(scores) >= 300, str(scores[-5:]))
    check("lives are lost", lives and min(lives) < 3, str(lives))
    check("game over", "state=gameover" in log)
    print(f"    score {max(scores)}, level {max(levels or [1])}, fps {min(fps or [0])}..{max(fps or [0])}")
    check("frame rate logged (>= 12 fps)", fps and min(fps) >= 12, str(fps))
    m = re.search(r"gameover score=(\d+)", log)
    final = int(m.group(1)) if m else 0
    e.run(5)
    e.screenshot(SHOTS / "gameover.png")
    more = ""
    for _ in range(100):
        e.run(5)
        more += e.log()
        if "hiscore rank" in more or "rb: title" in more:
            break
    check("the score makes the high-score table (defaults 1000..200)", "hiscore rank" in more, f"{final}")
    if "hiscore rank" in more:
        e.run(2)
        e.screenshot(SHOTS / "hiscore.png")
        e.type("BOT")
        e.run(10)
        more += e.log()
        check("high-score name entry", "rb: name=BOT" in more, more[-200:])
        check("back to the title", "rb: title" in more or e.run_until("rb: title", 300))
        e.run(20)
        e.screenshot(SHOTS / "hiscores.png")
        tbl = e.screenshot(layer="ula")
        check("title shows the table again", len(tbl.getcolors(1 << 16)) >= 4)
    rec = e.record_input("save", REC)
    print(f"  recorded {rec['frames']} frames, {rec['events']} input changes")

# the same game, replayed twice from the recording: identical every frame
seqs = []
for i in range(2):
    with Emulator() as e:
        r = e.replay(REC)
        seqs.append([h for _, h in r["hashes"]])
        check(f"replay {i + 1}: every frame hash matches the recorded game", r["ok"],
              f"{r.get('mismatches')} mismatches, first at {r.get('first_mismatch')}")
check("two replays give identical screens", seqs[0] == seqs[1] and len(seqs[0]) > 1000)

# a 128K: the AY plays the music (channels B and C) and the effects; the
# Kempston starts the game and turns the ship
with Emulator(machine="128k") as e:
    e.load(TAP)
    check("128K: boots, AY found", e.run_until("rb: ready kempston=1 ay=1", 4000))
    e.run(30)
    e.joystick("Kempston")
    e.press("FIRE")
    check("Kempston fire starts the game", e.run_until("rb: state=playing", 100))
    a0 = state(e)["angle"]
    e.hold("LEFT")
    e.run(20)
    e.release()
    check("Kempston left turns the ship", state(e)["angle"] != a0)
    e.hold("FIRE")
    e.run(300)
    e.release()
    log = e.log(0)
    m = re.findall(r"ay mix=(\d+) volb=(\d+) volc=(\d+)", log)
    check("AY music: tones on B and C, volumes up",
          m and any((int(x) & 6) != 6 and int(b) + int(c) > 0 for x, b, c in m), str(m[-3:]))

print(f"{'FAIL' if failures else 'PASS'}: rock_blaster")
sys.exit(1 if failures else 0)
