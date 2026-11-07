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
match). While the bot plays, every game loop's result is checked against the
game's rules built for the host (the same C, run for the same frames with
the same keys): the Z80 build, its asm pathfinding and the level unpacker
included, must match it byte for byte.

3. Level DEMO, where the guards can't reach the player: the bot collects
   gold for a while (the frame rate is measured here, screenshots/demo.png).
4. A 128K: the AY is found and the game runs.

docs/images/bullion_dash.gif is part 1's test play.
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
from bot import Bot, State, PLAY_ON  # noqa: E402
import ctypes  # noqa: E402
from PIL import Image  # noqa: E402

TAP = HERE / "build" / "bullion_dash.tap"
SHOTS = HERE / "screenshots"
REC = HERE / "build" / "bot-game.json"
START = 4                           # the bot takes some gold here before the guards get it
DEMO = 15                           # the guards can't reach the player here
GIVE_UP = 1500                      # frames without gold or a death: BREAK
GIF = ROOT / "docs" / "images" / "bullion_dash.gif"
failures = []
bot = Bot()


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


def state(e):
    return e.read_mem("gs", bot.size), e.read_mem("enemy_rng_state", 4)


mismatches = []
compared = [0]


def same_as_host(prev, gs, rng_prev, keys):
    """The frames between two looks, run on the host from the first look
    with the same keys, must give the second look's game state."""
    frames = (struct.unpack_from("<H", gs, 13)[0] - struct.unpack_from("<H", prev, 13)[0]) & 0xFFFF
    lib = bot.lib
    ctypes.memmove(lib.sim_gs(), prev, bot.size)
    ctypes.memmove(lib.sim_rng(), rng_prev, 4)
    lib.sim_sync()
    for _ in range(frames):
        if lib.sim_tick(keys) != PLAY_ON:
            return                                  # an event: main.c takes over
    host = ctypes.string_at(lib.sim_gs(), bot.size)
    compared[0] += frames
    if host[15:] != gs[15:] or host[1:13] != gs[1:13]:
        mismatches.append((struct.unpack_from("<H", gs, 13)[0],
                           [i for i in range(1, bot.size) if host[i] != gs[i] and not 13 <= i < 15][:8]))


def drive(e, until, max_loops=30000, shots=None, frames=None):
    """The bot plays: the emulator stops at each game loop's frame_wait, the
    bot reads gs and holds its keys. Returns (log, loops). frames: a list to
    collect (screenshot, frames shown) for a GIF."""
    bp = e.break_at("frame_wait")
    held, log, last_progress, last = "", "", 0, None
    prev = None
    loops = 0
    for loops in range(max_loops):
        e.continue_(50)
        gs, rng = state(e)
        s = State(gs)
        if prev and prev[0][0] == 1 and s.state == 1 and prev[2] is not None:
            same_as_host(prev[0], gs, prev[1], prev[2])
        bits = bot.decide(gs, rng)
        keys = Bot.key_names(bits)
        progress = (s.gold, s.lives, s.level, s.state)
        if progress != last:
            last, last_progress = progress, s.frame
        if s.state == 1 and (s.frame - last_progress) & 0xFFFF > GIVE_UP:
            keys, bits = "CAPS SPACE", None         # BREAK: give the life up
            last_progress = s.frame
        prev = (gs, rng, bits)
        if keys != held:
            e.release()
            if keys:
                e.hold(keys)
            held = keys
        log += e.log()
        if shots:
            shots(e, s, log)
        if frames is not None:
            frames.append([e.screenshot(), 0, s.frame])
        if until(log, s):
            break
    e.release()
    e.delete(bp["index"])
    return log, loops


def save_gif(frames, path):
    """Screenshots taken at each game loop -> a GIF at the game's speed."""
    for a, b in zip(frames, frames[1:]):
        a[1] = 20 * max(1, (b[2] - a[2]) & 0xFFFF)
    frames[-1][1] = 1000
    imgs = [f[0].convert("P", palette=Image.ADAPTIVE, colors=16) for f in frames]
    path.parent.mkdir(parents=True, exist_ok=True)
    imgs[0].save(path, save_all=True, append_images=imgs[1:], duration=[f[1] for f in frames],
                 loop=0, optimize=True)


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

    gif = []
    log, _ = drive(e, lambda log, s: "complete" in log or "death" in log, 3000, watch_trap, gif)
    check("the bot digs a hole and the guard falls in", bool(trapped), log[-300:])
    check("the gold is taken", "gold=1/1" in log, log[-300:])
    check("climbing to the top row with the gold clears the level", "level 0 complete" in log, log[-300:])
    e.run(20)
    gif.append([e.screenshot(SHOTS / "cleared.png"), 0, gif[-1][2] + 50])
    save_gif(gif, GIF)
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
    e.run(25)
    e.screenshot(SHOTS / "play.png")
    log, loops = drive(e, lambda log, s: "gameover" in log, 30000)
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

check("the Z80 plays as the host build of the rules, frame by frame", compared[0] > 200 and not mismatches,
      f"{compared[0]} frames compared, mismatches at {mismatches[:3]}")

# ---- 3. a level where the guards can't reach the player: gold, frame rate
with Emulator() as e:
    e.load(TAP)
    e.run_until("bullion: ready", 3000)
    e.run(10)
    for _ in range(DEMO - 1):
        e.press("P")
    e.press("SPACE")
    check(f"level {DEMO} starts", e.run_until(f"game started at level {DEMO}", 100))
    e.run(20)
    perf0 = struct.unpack("<2H", e.read_mem("bullion_perf", 4))
    start = State(state(e)[0]).frame
    shot = []

    def gold_shot(e, s, log):
        if not shot and s.gold >= 3:
            e.screenshot(SHOTS / "demo.png")
            shot.append(s.frame)

    log, _ = drive(e, lambda log, s: (s.frame - start) & 0xFFFF >= 600 or s.state != 1, 3000, gold_shot)
    perf = struct.unpack("<2H", e.read_mem("bullion_perf", 4))
    ticks, draws = perf[0] - perf0[0], perf[1] - perf0[1]
    fps = 50.0 * draws / max(ticks, 1)
    print(f"  level {DEMO}: {ticks} frames of game time, {draws} screens drawn: {fps:.1f} fps")
    check(f"the bot collects gold on level {DEMO}", "gold=4/" in log, log[-200:])
    check("the screen keeps up (>= 20 fps in play; the rules run at 50 Hz)", fps >= 20, f"{fps:.1f}")
check("still the host's rules, frame by frame", not mismatches, str(mismatches[:3]))

# ---- 4. the 128K: AY effects
with Emulator() as e:
    e.load(TAP, machine="128k")
    ok = e.run_until("bullion: ready", 3000)
    e.run(2)
    log = e.log_text(0)
    check("a 128K finds the AY", ok and "ay=1" in log, log[-100:])
    e.run(10)
    e.press("SPACE")
    check("the game runs on a 128K", e.run_until("state=playing", 100))
    e.run(200)
    check("... and plays on (a life lost to the guards)", "death caught" in e.log())

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
