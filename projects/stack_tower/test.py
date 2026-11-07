"""Stack Tower (Next) - plays through the harness.

First the rules as zsdcc compiles them (tests/z80) play the original's four
browser games (its tests/vectors.h) and must end as the browser did. Then the
game:
Boots to the title (28 MHz, the painting, then the demo game, the music on
TurboSound chips 1 and 2), turns the help pages, picks the mode and level
on the keys and the Kempston. Then, recorded from reset: a game where
tools/bot.py plays from the probe (src/game.c st_probe) with the keys - the
tower turns with the piece, rotate both ways, hold, ghost, pause - stacking
pieces and clearing rings; then it drops pieces where they are until the
tower tops out; game over, initials typed into the high-score table, back
to the title. The recording is replayed twice: every frame hash must
match. Screenshots go to screenshots/, the frame rate is reported.
"""
import re
import struct
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / "tools" / "zxdev" / "src"))
sys.path.insert(0, str(HERE / "tools"))

from zxdev.emu import Emulator  # noqa: E402
from bot import NAME, OVER, PLAY, TITLE, Bot  # noqa: E402

NEX = HERE / "build" / "stack_tower.nex"
SHOTS = HERE / "screenshots"
REC = HERE / "build" / "bot-game.json"
GOOD_PIECES = 40
failures = []


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


def gfx_define(name):
    m = re.search(rf"#define {name}\s+(\d+)", (HERE / "src" / "gfx_data.h").read_text())
    return int(m.group(1))


def ay(e):
    """the next 'ay' line (every 100 frames): how many of those frames chips 1 and 2 sounded"""
    mark = len(e.log(0))
    e.run(110)
    m = re.findall(r"ay chip1=(\d+) chip2=(\d+)", e.log(0)[mark:])
    return [int(v) for v in m[-1]] if m else None


def wait_for(bot, cond, frames=30):
    """the probe is written once a game loop: on a busy frame that is a few
    frames after a key"""
    for _ in range(frames):
        p = bot.probe()
        if cond(p):
            return True
        bot.e.run(1)
    return cond(bot.probe())


def perf(log, state):
    return [int(f) for f, s in re.findall(r"perf fps=(\d+) .*?state=(\d)", log) if int(s) == state]


SHOTS.mkdir(exist_ok=True)
build = subprocess.run([str(ROOT / "bin" / "zx-make"), "-C", str(HERE / "tests" / "z80")], capture_output=True, text=True)
check("the rules' Z80 test builds", build.returncode == 0, build.stdout[-300:] + build.stderr[-300:])
with Emulator(machine="next") as e:
    e.load(HERE / "tests" / "z80" / "build" / "stack_rules.nex")
    e.run_until("rules: done", 3000)
    e.run(1)
    log = e.log(0)
    check("the Z80's rules end the browser's four games as the browser did (pieces, score, board, queue, hold)",
          "rules: done 4 of 4" in log, log[-300:])

with Emulator(machine="next") as e:
    e.load(NEX)
    check("boots to the title", e.run_until("stack: ready", 300))
    e.run(2)
    check("28 MHz", "turbo=3" in e.log(0))
    bot = Bot(e)
    e.run(100)
    check("the title painting is shown (its own Layer 2 banks)", e._reg(0x12) == gfx_define("GFX_TITLE_BANK"))
    e.screenshot(SHOTS / "title.png")
    a = ay(e)
    check("the music plays on TurboSound chips 1 (lead, bass, organ) and 2 (drums)",
          a and a[0] > 50 and a[1] > 5, str(a))
    e.run(450)
    check("then the demo game: the tower on Layer 2", e._reg(0x12) in (gfx_define("GFX_SCR_BANK"),
                                                                       gfx_define("GFX_BACK_BANK")))
    e.run(200)
    check("the demo places pieces", bot.probe()["pieces"] > 3, str(bot.probe()["pieces"]))
    e.screenshot(SHOTS / "title_demo.png")

    e.log()
    e.press("H")
    check("H: the rules", e.run_until("help 1", 20))
    e.run(5)
    e.screenshot(SHOTS / "help.png")
    e.press("H")
    check("H: the controls", e.run_until("help 2", 20))
    e.press("H")
    check("H again: back", e.run_until("help 0", 20))

    e.press("P")
    check("P (right): the next mode, Rising", wait_for(bot, lambda p: p["sel_mode"] == 1))
    e.press("O")
    check("O (left): Marathon again", wait_for(bot, lambda p: p["sel_mode"] == 0))
    e.joystick("Kempston")
    e.press("LEFT")
    check("Kempston left: Dig (the modes go round)", wait_for(bot, lambda p: p["sel_mode"] == 2))
    e.press("RIGHT")
    check("Kempston right: Marathon", wait_for(bot, lambda p: p["sel_mode"] == 0))
    e.press("UP")
    check("Kempston up: level 2", wait_for(bot, lambda p: p["sel_level"] == 2))
    e.press("A")
    check("A (down): level 1", wait_for(bot, lambda p: p["sel_level"] == 1))

    # ---- the game, recorded from reset
    e.record_input("start", from_reset=True)
    e.run_until("stack: ready", 300)
    e.run(60)
    e.press("SPACE")
    check("SPACE starts Marathon at level 1 (the first seed)", e.run_until("start marathon level=1 seed=1", 30))
    check("playing: a piece at the top",
          wait_for(bot, lambda p: p["state"] == PLAY and p["pieces"] == 0 and p["prow"] >= 20), str(bot.probe()))
    p = bot.probe()
    col = p["pcol"]
    e.press("P")
    check("P turns the tower: the piece one column right", wait_for(bot, lambda p: p["pcol"] == (col + 1) % 16))
    e.run(40)
    check("the view turns to face it", bot.probe()["view"] == bot.probe()["pcol"] * 256)
    e.screenshot(SHOTS / "turned.png")
    rot = bot.probe()["prot"]
    e.press("X")
    check("X rotates clockwise", wait_for(bot, lambda p: p["prot"] == (rot + 1) % 4))
    e.press("Z")
    check("Z rotates back", wait_for(bot, lambda p: p["prot"] == rot))
    t = bot.probe()["ptype"]
    e.press("C")
    check("C holds the piece", wait_for(bot, lambda p: p["hold"] == t))
    e.log()
    e.press("ENTER")
    check("ENTER pauses", e.run_until("pause 1", 10))
    row = bot.probe()["prow"]
    e.run(100)
    check("paused: the piece stays", bot.probe()["prow"] == row)
    e.screenshot(SHOTS / "paused.png")
    e.press("ENTER")
    check("ENTER again: on", e.run_until("pause 0", 10))

    shot_at = {12: "playing", 30: "stacked"}
    while bot.probe()["pieces"] < GOOD_PIECES and bot.probe()["state"] == PLAY:
        p = bot.place()
        if p["pieces"] in shot_at:
            e.run(20)
            e.screenshot(SHOTS / f"{shot_at.pop(p['pieces'])}.png")
    p = bot.probe()
    full = e.log(0)
    check(f"the bot places {GOOD_PIECES} pieces", p["pieces"] >= GOOD_PIECES and p["state"] == PLAY, str(p))
    check("and clears rings", p["lines"] > 0, str(p["lines"]))
    rings = re.findall(r"ev clear a=(\d+) b=(\d+) score=(\d+) rings=(\d+)", full)
    check("each ring scores 100 x level (more for two at once, plus the combo)",
          rings and all(int(b) >= 100 * int(a) for a, b, *_ in rings), str(rings[:3]))
    fps = perf(full, PLAY)
    if fps:
        print(f"  frame rate while playing: {sum(fps) / len(fps):.1f} fps on average, {min(fps)} at the lowest "
              f"({len(fps)} samples of 100 frames)")
    check("25 fps or more on average while playing", fps and sum(fps) / len(fps) >= 25, str(fps))
    steps = [(int(s), int(f)) for s, f in re.findall(r"perf fps=\d+ steps=(\d+) frames=(\d+)", full)]
    check("the rules run 25 steps a second however fast it draws",
          all(abs(2 * s - f) <= 3 for s, f in steps), str(steps[:8]))

    # now the bot stops caring: every piece dropped where it is
    for _ in range(80):
        if bot.probe()["state"] != PLAY:
            break
        pieces = bot.probe()["pieces"]
        e.press("SPACE")
        for _ in range(20):
            if bot.probe()["pieces"] != pieces or bot.probe()["state"] != PLAY:
                break
            e.run(1)
    p = bot.probe()
    check("dropped in one place, the tower tops out: game over", p["state"] == OVER and p["over"], str(p))
    e.run(10)
    e.screenshot(SHOTS / "game_over.png")
    over = re.findall(r"over score=(\d+) rings=(\d+) pieces=(\d+) rank=(-?\d+)", e.log(0))
    check("over: the score is logged with its place in the table", over and int(over[-1][3]) >= 0, str(over))
    e.run(90)
    e.press("SPACE")
    check("FIRE: a high score, initials", e.run_until("hiscore rank=", 20) and bot.probe()["state"] == NAME)
    e.run(5)
    e.screenshot(SHOTS / "initials.png")
    e.type("BOT")
    check("typed BOT", e.run_until("name=BOT", 60))
    check("back to the title", e.run_until("state title", 30))
    table = e.read_mem("hs_table", 5 * 8)
    entries = [(table[i * 8:i * 8 + 3].decode(), struct.unpack("<L", table[i * 8 + 4:i * 8 + 8])[0]) for i in range(5)]
    check("BOT is in the Marathon table", any(n == "BOT" and s == p["score"] for n, s in entries), str(entries))
    e.run(500)
    e.screenshot(SHOTS / "title_table.png")
    rec = e.record_input("save", REC)
    print(f"  recorded {rec['frames']} frames, {rec['events']} input changes; score {p['score']}, "
          f"{p['pieces']} pieces, {p['lines']} rings")

seqs = []
for i in range(2):
    with Emulator(machine="next") as e:
        r = e.replay(REC)
        seqs.append([h for _, h in r["hashes"]])
        check(f"replay {i + 1}: every frame hash matches the recorded game", r["ok"],
              f"{r.get('mismatches')} mismatches, first at {r.get('first_mismatch')}")
check("two replays give identical screens", seqs[0] == seqs[1] and len(seqs[0]) > 1000)

print(f"{'FAIL' if failures else 'PASS'}: stack_tower")
sys.exit(1 if failures else 0)
