"""Planet Chomp (Next) - plays a whole game through the harness.

A bot plays with the keys: each step it reads bot_key, the screen direction
the original's own autopilot (bot_choose / bot_dodge, game.c bot_update)
wants, and holds that key (Q A O P). It eats crumbs, takes keys and eats a
frightened spook. Then it lets go: the spooks catch the chomper until the
game is over, the bot types its initials into the high-score table, and the
title comes back. The game is recorded from reset and replayed twice (every
frame hash must match). Screenshots go to screenshots/.
"""
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "zxdev" / "src"))

from zxdev.emu import Emulator  # noqa: E402

HERE = Path(__file__).resolve().parent
NEX = HERE / "build" / "planet_chomp.nex"
SHOTS = HERE / "screenshots"
REC = HERE / "build" / "bot-game.json"
KEYS = {0x0001: "Q", 0x0002: "A", 0x0004: "O", 0x0008: "P"}     # PAD_UP DOWN LEFT RIGHT
failures = []


def check(name, ok, detail=""):
    print(flush=True, end="")
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


def u16(e, sym):
    return struct.unpack("<H", e.read_mem(sym, 2))[0]


def bot(e, frames, until):
    """hold the autopilot's key, a step every 4 frames; stop when until(log)"""
    log, held = "", None
    for _ in range(frames // 4):
        k = KEYS.get(u16(e, "bot_key"))
        if k != held:
            e.release()
            if k:
                e.hold(k)
            held = k
        e.run(4)
        log += e.log()
        if until(log):
            break
    e.release()
    return log


SHOTS.mkdir(exist_ok=True)
with Emulator(machine="next") as e:
    e.load(NEX)
    check("boots to the title", e.run_until("pc: ready", 500))
    check("28 MHz", e._cmd("get-cpu-turbo-speed").strip() == "8")
    # the maze the Next carved is the original's (tools/gen_data.c ran its maze.c)
    want = (HERE / "build" / "level1.txt").read_text().split()[0]
    got = "".join("%x" % b for b in e.read_mem("mz_open", 486))
    check("level 1's maze is the original's (486 cells)", got == want, f"{sum(a != b for a, b in zip(got, want))} cells differ")
    e.run(300)
    log = e.log(0)
    e.run(400)
    log = e.log(0)
    m = re.findall(r"pc: ay vol0=(\d+) vol1=(\d+)", log)
    check("the title tune plays on chips 0 and 1", any(int(a) and int(b) for a, b in m), str(m[-3:]))
    e.screenshot(SHOTS / "title.png")

    e.record_input("start", from_reset=True)
    e.run_until("pc: ready", 500)
    # the attract demo: the original's autopilot plays the real game (a
    # frightened spook is first eaten 9405 steps in, the same on the host)
    check("the attract demo starts after 15 s", e.run_until("pc: demo starts", 3000))
    log = ""
    for _ in range(160):
        e.run(100)
        log += e.log()
        if "pc: eat " in log:
            break
    eats = re.findall(r"pc: eat (\w+) \+(\d+)", log)
    check("the demo eats crumbs, takes a key and eats a frightened spook (200)",
          "chomp score=" in log and "pc: key: invincible" in log and eats and eats[0][1] == "200", str(eats))
    e.run(6)
    e.screenshot(SHOTS / "eat.png")
    e.press("SPACE", hold_frames=12, release_frames=3)
    check("a key ends the demo: the title", e.run_until("pc: state=0 ", 100))
    e.run(60)
    e.press("SPACE", hold_frames=12, release_frames=3)
    check("SPACE starts a game", e.run_until("pc: state=1 ", 100))
    check("READY, then play", e.run_until("pc: state=2 ", 400))
    e.press("B", hold_frames=12, release_frames=3)              # the bot (an input: it replays)
    check("B: the bot plays", e.run_until("pc: bot=1", 60))
    shot = [False]

    def over(log):
        if not shot[0] and "chomp score=" in log:
            e.screenshot(SHOTS / "play.png")
            shot[0] = True
        return "pc: gameover" in log
    log = bot(e, 40000, over)
    scores = [int(s) for s in re.findall(r"chomp score=(\d+)", log)]
    check("the bot eats crumbs (score)", scores and max(scores) >= 500, str(scores[-3:]))
    check("it takes a key: the spooks are frightened", "pc: key: invincible" in log)
    fps = [int(f) for f in re.findall(r"perf fps10=(\d+)", log)]
    sound = re.findall(r"pc: ay vol0=(\d+) vol1=(\d+) vol2=(\d+)", log)
    check("effects and ambience play while playing", any(int(c) for _, _, c in sound) and any(int(b) for _, b, _ in sound),
          str(sound[-3:]))
    more = log
    if "pc: gameover" not in more:                                 # a good game: let go, the spooks win
        e.press("B", hold_frames=12, release_frames=3)
        for _ in range(1200):
            e.run(50)
            more += e.log()
            if "pc: gameover" in more:
                break
    deaths = more.count("pc: death caught by")
    check("lives are lost (caught by the spooks)", deaths >= 3, str(deaths))
    m = re.search(r"pc: gameover score=(\d+) rank=(-?\d+)", more)
    check("game over", m is not None, more[-300:])
    final, rank = (int(m.group(1)), int(m.group(2))) if m else (0, -1)
    e.run(20)
    e.screenshot(SHOTS / "gameover.png")
    if rank >= 0:
        e.run(2)
        e.type("BOT")
        e.run(10)
        more += e.log()
        check("high-score initials", "pc: name=BOT" in more, more[-200:])
    else:
        check(f"score {final} below the table: no entry", final <= 1000)
    check("back to the title", e.run_until("pc: title", 200) or "pc: title" in more)
    e.run(260)                                                    # the title's high-score page
    e.screenshot(SHOTS / "hiscores.png")
    rec = e.record_input("save", REC)
    print(f"  final score {final}, {deaths} deaths, fps {[f / 10 for f in fps]}; "
          f"recorded {rec['frames']} frames, {rec['events']} input changes")

seqs = []
for i in range(2):
    with Emulator(machine="next") as e:
        r = e.replay(REC)
        seqs.append([h for _, h in r["hashes"]])
        check(f"replay {i + 1}: every frame hash matches the recorded game", r["ok"],
              f"{r.get('mismatches')} mismatches, first at {r.get('first_mismatch')}")
check("two replays give identical screens", seqs[0] == seqs[1] and len(seqs[0]) > 1000)

print(f"{'FAIL' if failures else 'PASS'}: planet_chomp")
sys.exit(1 if failures else 0)
