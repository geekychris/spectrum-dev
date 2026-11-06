"""Nova Defense (48K) - plays a whole game through the harness.

Boot -> title -> start -> a bot sweeps and fires (scores points, gets shot)
-> game over -> high-score entry -> back to the title. The bot's game is
recorded and replayed twice: every frame hash must match. Screenshots go to
projects/nova_defense/screenshots/.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "zxdev" / "src"))

from zxdev.emu import Emulator  # noqa: E402

HERE = Path(__file__).resolve().parent
TAP = HERE / "build" / "nova_defense.tap"
SHOTS = HERE / "screenshots"
REC = HERE / "build" / "bot-game.json"
failures = []


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


def probe(e):
    b = e.read_mem("nova_probe", 15)
    w = lambda i: int.from_bytes(b[i:i + 2], "little", signed=True)
    return w(0), [w(2), w(4), w(6)], [w(8), w(10), w(12)]


def bot_game(e, shots=True):
    """Play until the game is over: fire all the time; in the first phase sweep
    left/right, later walk under the nearest falling alien bullet (to get shot)."""
    log = ""
    t = 0
    while "state=gameover" not in log and t < 15000:
        px, bx, by = probe(e)
        target = None
        if t > 400:                                   # time to lose some lives
            live = [(by[i], bx[i]) for i in range(3) if bx[i] >= 0]
            if live:
                target = max(live)[1]
        if target is not None:
            direction = "O" if target < px + 7 else "P"
        else:
            direction = "O" if (t // 120) % 2 else "P"
        e.hold(direction)
        e.run(3)
        e.release()
        e.press("SPACE", hold_frames=2, release_frames=2)
        t += 7
        log += e.log()
        if shots and 700 <= t < 707:
            e.screenshot(SHOTS / "play.png")
    return log


with Emulator() as e:
    e.load(TAP)
    check("boots to the title", e.run_until("nova: ready", 3000))
    e.run(60)
    e.screenshot(SHOTS / "title.png")
    title = e.screenshot()
    check("title screen drawn", len(title.getcolors(1 << 16)) >= 5)
    e.record_input("start", from_reset=True)       # the whole run, for the replay check
    e.run_until("nova: ready", 3000)
    e.run(30)
    e.press("SPACE")
    check("FIRE starts the game", e.run_until("nova: state=playing", 100))
    log = bot_game(e)
    scores = [int(s) for s in re.findall(r"nova: score=(\d+)", log)]
    lives = [int(s) for s in re.findall(r"nova: lives=(\d+)", log)]
    check("the bot scores points", scores and max(scores) >= 100, str(scores[-5:]))
    check("a life is lost", lives and min(lives) < 3, str(lives))
    check("game over", "state=gameover" in log)
    m = re.search(r"gameover score=(\d+) overruns=(\d+)", log)
    final = int(m.group(1)) if m else 0
    e.run(5)
    e.screenshot(SHOTS / "gameover.png")
    # high-score entry if it qualifies (defaults 2500..500), else straight to the title
    more = ""
    for _ in range(200):
        e.run(5)
        more += e.log()
        if "hiscore rank" in more or "nova: title" in more:
            break
    if "hiscore rank" in more:
        e.screenshot(SHOTS / "hiscore.png")
        e.type("BOT")
        e.run(10)
        more += e.log()
        check("high-score name entry", "nova: name=BOT" in more, more[-200:])
        e.run_until("nova: title", 300)
        more += "nova: title"
    else:
        check(f"score {final} below the table: no entry", final <= 100)
    check("back to the title", "nova: title" in more)
    e.run(20)
    if "name=BOT" in more:
        e.screenshot(SHOTS / "hiscores.png")
        check("BOT in the high-score table", True)
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

print(f"{'FAIL' if failures else 'PASS'}: nova_defense")
sys.exit(1 if failures else 0)
