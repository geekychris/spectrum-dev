"""Bullion Dash (48K) - plays the game through the harness.

1. The title; E opens the editor. The test builds a small level there (a
   brick floor, a ladder to the top, gold, the player and a guard right next
   to him) and test-plays it (ENTER): the bot (tools/bot.py, which plans with
   the game's own rules built for the host) has to dig a hole in front of the
   guard to trap him, take the gold and climb out: LEVEL COMPLETE.
2. Back on the title it starts a game at level START (O/P choose the level):
   the original's guards are faster than the player, so the bot loses its
   lives (giving one up with BREAK if it is stuck), game over, initials,
   the title again.

The whole session is recorded and replayed twice (every frame hash must
match). Screenshots go to screenshots/, the frame rate is measured in play.
"""
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / "tools" / "zxdev" / "src"))
sys.path.insert(0, str(HERE / "tools"))

from zxdev.emu import Emulator  # noqa: E402
from bot import Bot, State  # noqa: E402

TAP = HERE / "build" / "bullion_dash.tap"
SHOTS = HERE / "screenshots"
REC = HERE / "build" / "bot-game.json"
START = 4                           # the bot takes some gold here before the guards get it
GIVE_UP = 1500                      # frames without gold or a death: BREAK
failures = []
bot = Bot()


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


def state(e):
    return e.read_mem("gs", bot.size), e.read_mem("enemy_rng_state", 4)


def drive(e, until, max_loops=30000, shots=None):
    """The bot plays: the emulator stops at each game loop's frame_wait, the
    bot reads gs and holds its keys. Returns (log, loops, ticks)."""
    bp = e.break_at("frame_wait")
    held, log, last_progress, last = "", "", 0, None
    loops = 0
    for loops in range(max_loops):
        e.continue_(50)
        gs, rng = state(e)
        s = State(gs)
        keys = Bot.key_names(bot.decide(gs, rng))
        progress = (s.gold, s.lives, s.level, s.state)
        if progress != last:
            last, last_progress = progress, s.frame
        if s.state == 1 and (s.frame - last_progress) & 0xFFFF > GIVE_UP:
            keys = "CAPS SPACE"                 # BREAK: give the life up
            last_progress = s.frame
        if keys != held:
            e.release()
            if keys:
                e.hold(keys)
            held = keys
        log += e.log()
        if shots:
            shots(e, s, log)
        if until(log, s):
            break
    e.release()
    e.delete(bp["index"])
    return log, loops


def ed_cursor(e):
    return e.peek("ed_x"), e.peek("ed_y"), e.peek("ed_tile")


def ed_goto(e, x, y, paint=False):
    """Move the editor's cursor one press at a time (SPACE held: paint)."""
    for _ in range(80):
        cx, cy, _ = ed_cursor(e)
        if (cx, cy) == (x, y):
            break
        key = "P" if cx < x else "O" if cx > x else "A" if cy < y else "Q"
        e.press(f"{key} SPACE" if paint else key, hold_frames=2, release_frames=6)
    if paint:
        e.press("SPACE", hold_frames=2, release_frames=2)


def ed_tile(e, t):
    for _ in range(12):
        if ed_cursor(e)[2] == t:
            return
        e.press("X", hold_frames=2, release_frames=4)


def put(e, x, y, t):
    ed_goto(e, x, y)
    ed_tile(e, t)
    e.press("SPACE", hold_frames=2, release_frames=2)


EMPTY, BRICK, SOLID, LADDER, BAR, GOLD, HIDDEN, PLAYER, ENEMY, TRAP = range(10)

with Emulator() as e:
    e.load(TAP)
    check("boots to the title", e.run_until("bullion: ready", 3000))
    e.record_input("start", from_reset=True)
    e.run_until("bullion: ready", 3000)
    e.run(60)
    e.screenshot(SHOTS / "title.png")

    # ---- 1. the editor: a level to clear
    e.press("E")
    check("E opens the editor", e.run_until("bullion: editor", 50))
    e.run(5)
    ed_tile(e, BRICK)
    ed_goto(e, 0, 15)
    ed_goto(e, 7, 15, paint=True)               # a brick floor under the left part
    ed_tile(e, LADDER)
    ed_goto(e, 1, 0)
    ed_goto(e, 1, 14, paint=True)               # a ladder to the top row
    put(e, 0, 14, GOLD)
    put(e, 2, 14, PLAYER)
    put(e, 4, 14, ENEMY)                        # a guard two cells away
    gs, _ = state(e)
    t = State(gs).tile
    check("the editor puts the tiles", t(3, 15) == BRICK and t(1, 7) == LADDER and t(0, 14) == GOLD
          and t(2, 14) == PLAYER and t(4, 14) == ENEMY, f"{t(3, 15)} {t(1, 7)} {t(0, 14)} {t(2, 14)} {t(4, 14)}")
    e.run(10)
    e.screenshot(SHOTS / "editor.png")
    e.press("ENTER")
    check("ENTER test-plays the level", e.run_until("bullion: test play", 50))

    trapped = []

    def watch_trap(e, s, log):
        if any(en[4] == 4 or (en[3] == 15 and en[2] == 3) for en in s.enemies) and not trapped:
            trapped.append(s.frame)
            e.screenshot(SHOTS / "trap.png")

    log, _ = drive(e, lambda log, s: "complete" in log or "death" in log, 3000, watch_trap)
    check("the bot digs a hole and the guard falls in", bool(trapped), log[-300:])
    check("the gold is taken", "gold=1/1" in log, log[-300:])
    check("climbing to the top row with the gold clears the level", "level 0 complete" in log, log[-300:])
    e.run(20)
    e.screenshot(SHOTS / "cleared.png")
    check("back to the editor after the test play", e.run_until("state=editor", 200))
    e.run(30)                                   # the editor's screen is redrawn
    e.press("CAPS SPACE", hold_frames=10)
    ok = e.run_until("bullion: title", 100)
    check("BREAK leaves the editor", ok, f"state={e.peek('gs')} log={e.log()[-300:]!r}")

    # ---- 2. a game from level START: lives lost, game over, initials
    e.run(20)
    for _ in range(START - 1):
        e.press("P")
    e.press("SPACE")
    check(f"FIRE starts the game at level {START}", e.run_until(f"game started at level {START}", 100))
    bullion_perf0 = struct.unpack("<2H", e.read_mem("bullion_perf", 4))
    shots = set()

    def playing_shot(e, s, log):
        if "play" not in shots and s.state == 1 and s.gold >= 1:
            e.screenshot(SHOTS / "play.png")
            shots.add("play")

    log, loops = drive(e, lambda log, s: "gameover" in log, 30000, playing_shot)
    perf = struct.unpack("<2H", e.read_mem("bullion_perf", 4))
    ticks, draws = perf[0] - bullion_perf0[0], perf[1] - bullion_perf0[1]
    fps = 50.0 * draws / max(ticks, 1)
    print(f"  play: {ticks} frames of game time, {draws} screens drawn: {fps:.1f} fps")
    check("the screen keeps up (>= 25 fps in play)", fps >= 25, f"{fps:.1f}")
    scores = [int(v) for v in re.findall(r"bullion: score=(\d+)", log)]
    lives = [int(v) for v in re.findall(r"bullion: lives=(\d+)", log)]
    deaths = re.findall(r"bullion: death (\w+(?: up)?)", log)
    check("gold scores 100 a piece", scores and max(scores) >= 100 and all(v % 100 == 0 for v in scores), str(scores[-5:]))
    check("lives are lost", lives and min(lives) == 0, str(lives))
    check("deaths by the guards (or given up)", deaths and set(deaths) <= {"caught", "buried", "gave up"}
          and "caught" in deaths, str(deaths))
    check("game over", "bullion: gameover" in log)
    m = re.search(r"gameover score=(\d+)", log)
    final = int(m.group(1)) if m else 0
    e.run(40)
    e.screenshot(SHOTS / "gameover.png")
    e.press("SPACE")
    more = ""
    for _ in range(100):
        e.run(5)
        more += e.log()
        if "hiscore rank" in more or "bullion: title" in more:
            break
    if "hiscore rank" in more:
        e.run(10)
        e.screenshot(SHOTS / "initials.png")
        e.type("BOT")
        e.run(10)
        more += e.log()
        check("high-score name entry", "bullion: name=BOT" in more, more[-200:])
        e.run_until("bullion: title", 300)
        more += "bullion: title"
    else:
        check(f"score {final} below the table: no entry", final < 100)
    check("back to the title", "bullion: title" in more)
    e.run(30)
    e.screenshot(SHOTS / "hiscores.png")
    rec = e.record_input("save", REC)
    print(f"  recorded {rec['frames']} frames, {rec['events']} input changes, final score {final}")

seqs = []
for i in range(2):
    with Emulator() as e:
        r = e.replay(REC)
        seqs.append([h for _, h in r["hashes"]])
        check(f"replay {i + 1}: every frame hash matches the recorded game", r["ok"],
              f"{r.get('mismatches')} mismatches, first at {r.get('first_mismatch')}")
check("two replays give identical screens", seqs[0] == seqs[1] and len(seqs[0]) > 500)

print(f"{'FAIL' if failures else 'PASS'}: bullion_dash")
sys.exit(1 if failures else 0)
