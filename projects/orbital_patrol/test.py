"""Orbital Patrol (Next) - plays a whole game through the harness.

Boots to the title (the MOD playing), then, recorded from reset: a bot
reads the game's state (`gs`, src/game.h's structs, in 8K page 46) every
frame and flies: it chases the nearest enemy, lines up with it, faces it
and fires, smart-bombs a crowd; after a while it stops shooting and
hovers, so lives are lost: game over, the initials, the high scores, back
to the title. The recording is replayed twice (every frame hash must
match). Then the same game's input, as the game recorded it tick by tick
(page 47), is played through the original's code built on the host
(tools/ref.c): the state every 25 ticks must be the same.

Screenshots go to screenshots/; --gif makes docs/images/orbital_patrol.gif;
--debug prints the bot now and then.
"""
import re
import struct
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / "tools" / "zxdev" / "src"))

from zxdev.emu import Emulator  # noqa: E402

NEX = HERE / "build" / "orbital_patrol.nex"
SHOTS = HERE / "screenshots"
REC = HERE / "build" / "bot-game.json"
REF = HERE / "build" / "ref"
DEBUG = "--debug" in sys.argv
failures = []

# src/game.h on the Z80 (packed, little-endian), at 0x0000 of page 46
SHIP, ENEMY, HUMAN, BULLET, MINE = 0, 30, 510, 690, 1010
E_SIZE, H_SIZE, B_SIZE, M_SIZE = 30, 18, 16, 10
EBULLET = BULLET + 8 * B_SIZE
VIEWPORT, SCORE, LIVES, LEVEL, STATE = 5562, 5566, 5574, 5576, 5578
WORLD_FP = 1920 * 256


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


def wdist(a, b):
    d = a - b
    if d > WORLD_FP // 2:
        d -= WORLD_FP
    if d < -WORLD_FP // 2:
        d += WORLD_FP
    return d


def read_gs(e):
    b = e.read_mem(0, 5600, page=46)
    i32 = lambda o: struct.unpack_from("<i", b, o)[0]
    i16 = lambda o: struct.unpack_from("<h", b, o)[0]
    ship = dict(x=i32(0), y=i32(4), facing=i16(16), alive=i16(18), bombs=i16(22))
    enemies = [dict(x=i32(ENEMY + k * E_SIZE), y=i32(ENEMY + k * E_SIZE + 4), type=i16(ENEMY + k * E_SIZE + 18))
               for k in range(16) if i16(ENEMY + k * E_SIZE + 16)]
    shots = [dict(x=i32(EBULLET + k * B_SIZE), y=i32(EBULLET + k * B_SIZE + 4))
             for k in range(12) if i16(EBULLET + k * B_SIZE + 12)]
    mines = [dict(x=i32(MINE + k * M_SIZE), y=i32(MINE + k * M_SIZE + 4))
             for k in range(12) if i16(MINE + k * M_SIZE + 8)]
    return dict(ship=ship, enemies=enemies, shots=shots, mines=mines, score=i32(SCORE),
                lives=i16(LIVES), level=i16(LEVEL), state=i16(STATE), vx=i32(VIEWPORT))


def steer(e, keys, held):
    if keys != held:
        e.release()
        if keys:
            e.hold("+".join(keys))
    return keys


def decide(g, careless):
    """The keys for this frame."""
    s, keys = g["ship"], []
    if g["state"] not in (1, 3) or not s["alive"] or not g["enemies"]:
        return keys
    sx, sy = s["x"], s["y"] + 3 * 256
    near = min(g["enemies"], key=lambda e: abs(wdist(e["x"], sx)) + 2 * abs(e["y"] - s["y"]))
    dx = wdist(near["x"] + 5 * 256, sx + 8 * 256) // 256
    dy = (near["y"] + 5 * 256 - sy) // 256
    if careless:                               # hover in the middle, shoot nothing
        if sy // 256 > 130:
            keys.append("Q")
        elif sy // 256 < 110:
            keys.append("A")
        return keys
    # danger: an enemy, a shot or a mine close: out of its way (up or down)
    for d in g["shots"] + g["mines"] + g["enemies"]:
        ddx, ddy = wdist(d["x"], sx) // 256, (d["y"] - sy) // 256
        if abs(ddx) < (40 if d in g["enemies"] else 70) and abs(ddy) < 18:
            keys.append("A" if ddy <= 0 and sy // 256 < 190 or sy // 256 < 50 else "Q")
            if abs(ddx) < 25:
                keys.append("O" if ddx > 0 else "P")      # and away
            return keys
    want_right = dx > 0
    if abs(dx) > 110 or (want_right != (s["facing"] > 0) and abs(dx) > 40):
        keys.append("P" if want_right else "O")      # thrust towards it (and face it)
    if dy > 2:
        keys.append("A")
    elif dy < -2:
        keys.append("Q")
    if abs(dy) < 6 and (s["facing"] > 0) == want_right and abs(dx) < 280:
        keys.append("SPACE")
    on_screen = [e for e in g["enemies"] if abs(wdist(e["x"], sx)) < 150 * 256]
    if len(on_screen) >= 4 and s["bombs"] > 0:
        keys.append("Z")
    return keys


def bot_game(e, careful_frames=5000):
    log, t, held, shots = "", 0, (), set()
    while "op: gameover" not in log and t < 25000:
        g = read_gs(e)
        keys = decide(g, t > careful_frames)
        held = steer(e, tuple(keys), held)
        e.run(1)
        t += 1
        log += e.log()
        if DEBUG and t % 500 == 0:
            print("   t", t, "score", g["score"], "level", g["level"], "lives", g["lives"],
                  "enemies", len(g["enemies"]), "state", g["state"])
        if "play" not in shots and g["state"] == 1 and len(g["enemies"]) >= 1 and t > 200:
            on = [x for x in g["enemies"] if abs(wdist(x["x"], g["ship"]["x"])) < 140 * 256]
            if on:
                e.screenshot(SHOTS / "play.png")
                shots.add("play")
        if "clear" not in shots and "state=level_clear" in log:
            e.run(30)
            e.screenshot(SHOTS / "wave_clear.png")
            shots.add("clear")
        if "bomb" not in shots and re.search(r"bombs=2", log):
            e.run(3)
            e.screenshot(SHOTS / "smart_bomb.png")
            shots.add("bomb")
    steer(e, (), held)
    return log, t


SHOTS.mkdir(exist_ok=True)
with Emulator(machine="next") as e:
    e.load(NEX)
    check("boots to the title", e.run_until("op: ready", 600))
    e.run(80)
    e.screenshot(SHOTS / "title.png")
    r0 = e.peek16("_row")
    e.run(100)
    check("the MOD plays (TurboSound chips 1 and 2)", e.peek16("_row") != r0)

# the game, recorded from the load in a fresh session (on the Next a reset
# leaves the banks as they were, so a replay must start from the same state)
with Emulator(machine="next") as e:
    e.load(NEX)
    e.record_input("start", from_reset=True)     # (loads it again, from the start)
    e.run_until("op: ready", 600)
    e.run(60)
    e.press("SPACE")
    check("FIRE starts the game", e.run_until("op: state=level_start level=1", 100))
    check("the wave starts", e.run_until("op: state=playing", 200))
    log, frames = bot_game(e)
    scores = [int(x) for x in re.findall(r"op: score=(\d+)", log)]
    lives = [int(x) for x in re.findall(r"op: lives=(\d+)", log)]
    fps = [int(x) for x in re.findall(r"op: fps=(\d+)", log)]
    check("enemies shot (points)", scores and max(scores) >= 1000, str(scores[-5:]))
    check("a wave cleared (the humans' bonus, wave 2)", "state=level_clear" in log and "op: level=2" in log)
    check("lives lost", lives and min(lives) == 0, str(lives))
    check("game over", "op: gameover" in log)
    check("frame rate (logged every 5 s) >= 25", fps and min(fps[1:] or fps) >= 25, str(fps))
    print(f"  fps in play: {fps}; {frames} frames")
    e.run(60)
    e.screenshot(SHOTS / "gameover.png")
    m = re.search(r"gameover score=(\d+)", log)
    final = int(m.group(1)) if m else 0
    more = ""
    for _ in range(80):
        e.run(5)
        more += e.log()
        if "hiscore rank" in more or "op: state=scores" in more:
            break
    if "hiscore rank" in more:
        e.run(10)
        e.screenshot(SHOTS / "initials.png")
        e.type("BOT")
        e.run(10)
        more += e.log()
        check("high-score name entry", "op: name=BOT" in more, more[-200:])
    else:
        check(f"score {final} below the table: no entry", final < 10000)
    check("the high scores", "op: state=scores" in more or e.run_until("op: state=scores", 200))
    e.run(40)
    e.screenshot(SHOTS / "hiscores.png")
    e.press("SPACE")
    check("back to the title", e.run_until("op: title", 200))
    rec = e.record_input("save", REC)
    tape = e.read_mem(0, 8192, page=47)
    full_log = e.log(0)
    print(f"  recorded {rec['frames']} frames, final score {final}")

seqs = []
for i in range(2):
    with Emulator(machine="next") as e:
        r = e.replay(REC)
        seqs.append([h for _, h in r["hashes"]])
        check(f"replay {i + 1}: every frame hash matches the recorded game", r["ok"],
              f"{r.get('mismatches')} mismatches, first at {r.get('first_mismatch')}")
check("two replays give identical screens", seqs[0] == seqs[1] and len(seqs[0]) > 1000)

# the original's code on the host, the same input tick by tick
z80 = [l for l in full_log.splitlines() if l.startswith("op: chk")]
r = subprocess.run(["cc", "-O2", "-I", str(HERE / "src"), "-o", str(REF), str(HERE / "tools" / "ref.c"),
                    str(HERE / "src" / "game.c"), str(HERE / "src" / "flow.c")], capture_output=True, text=True)
check("the host reference builds", r.returncode == 0, r.stderr[-300:])
if r.returncode == 0 and z80:
    (HERE / "build" / "tape.bin").write_bytes(tape)
    last = int(re.search(r"t=(\d+)", z80[-1]).group(1))
    host = subprocess.run([str(REF), str(HERE / "build" / "tape.bin"), str(last)],
                          capture_output=True, text=True).stdout.splitlines()
    diff = next(((a, b) for a, b in zip(z80, host) if a != b), None)
    check(f"the original's code on the host: the same state at all {len(z80)} checkpoints",
          len(z80) == len(host) and diff is None, f"{len(z80)} vs {len(host)}; {diff}")

if "--gif" in sys.argv:
    with Emulator(machine="next") as e:          # the bot again, filmed
        e.load(NEX)
        e.run_until("op: ready", 600)
        e.run(60)
        e.press("SPACE")
        e.run_until("op: state=playing", 200)
        held = ()
        for _ in range(300):
            held = steer(e, tuple(decide(read_gs(e), False)), held)
            e.run(1)
        frames = []
        for k in range(150):
            held = steer(e, tuple(decide(read_gs(e), False)), held)
            e.run(1)
            if k % 2 == 0:
                frames.append(e.screenshot().convert("RGB"))
        frames[0].save(ROOT / "docs" / "images" / "orbital_patrol.gif", save_all=True,
                       append_images=frames[1:], loop=0, duration=40, optimize=True)
        print("  docs/images/orbital_patrol.gif")

print(f"{'FAIL' if failures else 'PASS'}: orbital_patrol")
sys.exit(1 if failures else 0)
