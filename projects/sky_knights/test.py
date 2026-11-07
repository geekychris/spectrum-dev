"""Sky Knights (48K) - plays a whole game through the harness.

A bot reads the game state (`gs`, by symbol; the structs of src/game.h) and
jousts: it flies above the nearest enemy and drops onto it, and catches the
pods. Later it stops flapping and drifts under the enemies, so lives are
lost: game over, the initials editor, back to the title. The game is
recorded and replayed twice (every frame hash must match). Then:

- two players: keys move player 1, the Kempston player 2, each only theirs
- the per-frame loops in assembly (logic.asm, phys.asm) against the
  original C they replace: the game records the input each 50 Hz tick took
  (a test hook in main.c); a C build (PHYS_IN_C=1) and the game play those
  ticks back and must log the same game, move for move
- the 128K: the AY plays the melody

Screenshots go to screenshots/; `--debug` prints the bot's state now and then.
"""
import random
import re
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "zxdev" / "src"))

from zxdev.emu import Emulator  # noqa: E402

HERE = Path(__file__).resolve().parent
TAP = HERE / "build" / "sky_knights.tap"
CREF = HERE / "build" / "cref" / "sky_knights.tap"
SHOTS = HERE / "screenshots"
REC = HERE / "build" / "bot-game.json"
DEBUG = "--debug" in sys.argv
failures = []

# src/game.h: Player 26 bytes, Enemy 22, Egg 14, then Platform[8], num, score[2], wave, state
PLAYER, ENEMY, EGG = 26, 22, 14
OFS_ENEMIES = 2 * PLAYER
OFS_EGGS = OFS_ENEMIES + 8 * ENEMY
OFS_SCORE = OFS_EGGS + 8 * EGG + 8 * 6 + 2
OFS_WAVE = OFS_SCORE + 8
GS_SIZE = OFS_WAVE + 12


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


def read_gs(e):
    b = e.read_mem("gs", GS_SIZE)
    w = lambda o: struct.unpack_from("<h", b, o)[0]
    players = [dict(x=w(i * PLAYER) >> 6, y=w(i * PLAYER + 2) >> 6, dy=w(i * PLAYER + 6),
                    alive=w(i * PLAYER + 10), lives=w(i * PLAYER + 12)) for i in range(2)]
    enemies = [dict(x=w(OFS_ENEMIES + i * ENEMY) >> 6, y=w(OFS_ENEMIES + i * ENEMY + 2) >> 6,
                    type=w(OFS_ENEMIES + i * ENEMY + 8))
               for i in range(8) if w(OFS_ENEMIES + i * ENEMY + 10)]
    eggs = [dict(x=w(OFS_EGGS + i * EGG) >> 6, y=w(OFS_EGGS + i * EGG + 2) >> 6)
            for i in range(8) if w(OFS_EGGS + i * EGG + 6)]
    score = struct.unpack_from("<i", b, OFS_SCORE)[0]
    return dict(p=players[0], p2=players[1], enemies=enemies, eggs=eggs, score=score,
                wave=w(OFS_WAVE), state=w(OFS_WAVE + 2))


def wrap_dx(tx, px):
    d = tx - px
    while d > 170:
        d -= 341
    while d < -170:
        d += 341
    return d


def steer(e, keys, held):
    if keys != held:
        e.release()
        if keys:
            e.hold("+".join(keys))
    return keys


TAPE, TAPE_LEN = 0x6900, 2304        # src/mem.h MEM_TEST_TAPE


def play_tape(e, tape):
    """From now on the ticks take their input from tape (main.c's test hooks)."""
    e.write_mem(TAPE, tape)
    e.write_mem("tick_left", struct.pack("<H", len(tape)))
    e.write_mem("tick_tape", struct.pack("<H", TAPE))


def bot_game(e, careful_frames=6000):
    """Joust until careful_frames, then drift under the enemies until the game is over."""
    log, t, held, shots = "", 0, (), set()
    while "sky: gameover" not in log and t < 30000:
        g = read_gs(e)
        p, keys = g["p"], []
        if g["state"] == 1 and p["alive"]:
            careless = t > careful_frames
            if g["eggs"] and not careless:                 # pods first: they hatch into worse
                tgt = min(g["eggs"], key=lambda o: abs(wrap_dx(o["x"], p["x"])))
                want_y, tx = tgt["y"] - 12, tgt["x"] - 6
            elif g["enemies"]:
                tgt = min(g["enemies"], key=lambda o: abs(wrap_dx(o["x"], p["x"])) + abs(o["y"] - p["y"]))
                tx = tgt["x"]
                want_y = tgt["y"] + 30 if careless else tgt["y"] - 16   # careless: stay below it
            else:
                tx, want_y = p["x"], p["y"]
            dx = wrap_dx(tx, p["x"])
            if dx > 3:
                keys.append("P")
            elif dx < -3:
                keys.append("O")
            if p["y"] > want_y and (not careless or p["y"] > 120):
                keys.append("SPACE")
        held = steer(e, tuple(keys), held)
        e.run(1)
        t += 1
        new = e.log()
        log += new
        if DEBUG and t % 250 == 0:
            print("   t", t, g["score"], g["wave"], p, len(g["enemies"]), len(g["eggs"]))
        if "play" not in shots and t > 400 and len(g["enemies"]) >= 2:
            e.screenshot(SHOTS / "play.png")
            shots.add("play")
        if "pod" not in shots and g["eggs"] and t > 300:
            e.screenshot(SHOTS / "pod.png")
            shots.add("pod")
        if "intro" not in shots and "state=intro wave=2" in log:
            e.run(20)
            e.screenshot(SHOTS / "wave.png")
            shots.add("intro")
    steer(e, (), held)
    return log


def build_cref():
    """The game with the original C loops (PHYS_IN_C=1), at 0x7200 (it is bigger)."""
    r = subprocess.run([str(ROOT / "bin" / "zx-make"), "-C", str(HERE), "BUILD=build/cref",
                        "EXTRA_CFLAGS=-DPHYS_IN_C=1", "ORG=29184"], capture_output=True, text=True)
    return r.returncode == 0 and CREF.exists(), r.stdout[-400:] + r.stderr[-400:]


def tape_game(tap, tape):
    """The bot's game again from its recorded ticks of input: the game log.
    Each tick gets the same input whatever the build's speed, so the two
    builds must play exactly the same game."""
    with Emulator() as e:
        e.load(tap)
        e.run_until("sky: ready", 3000)
        play_tape(e, tape)
        e.run_until("sky: gameover", 6000)
        e.run(1)
        return "\n".join(l for l in e.log(0).splitlines() if l.startswith("sky:") and "fps=" not in l)


SHOTS.mkdir(exist_ok=True)
with Emulator() as e:
    e.load(TAP)
    check("boots to the title", e.run_until("sky: ready", 3000))
    e.run(60)
    check("structs as draw.asm/logic.asm expect", "layout mismatch" not in e.log(0))
    e.screenshot(SHOTS / "title.png")
    e.record_input("start", from_reset=True)
    e.run_until("sky: ready", 3000)
    e.run(40)
    e.press("SPACE")
    check("FIRE starts a one-player game", e.run_until("sky: state=intro wave=1 players=1", 100))
    check("the wave starts", e.run_until("sky: state=playing", 200))
    log = bot_game(e)
    scores = [int(s) for s in re.findall(r"sky: score p=1 s=(\d+)", log)]
    lives = [int(s) for s in re.findall(r"sky: lives p=1 n=(\d+)", log)]
    fps = [int(s) for s in re.findall(r"sky: fps=(\d+)", log)]
    check("jousts won", log.count("joust won") >= 3, f"{log.count('joust won')} won")
    check("pods caught", "pod caught" in log)
    check("a wave cleared (bonus, wave 2)", "state=intro wave=2" in log)
    check("points scored", scores and max(scores) >= 2000, str(scores[-5:]))
    check("lives lost", lives and min(lives) == 0, str(lives))
    check("game over", "sky: gameover" in log)
    check("the screen keeps up (fps logged >= 15)", fps and min(fps[1:] or fps) >= 15, str(fps))
    print(f"  fps in play: {fps}")
    m = re.search(r"gameover s1=(\d+)", log)
    final = int(m.group(1)) if m else 0
    e.run(40)
    e.screenshot(SHOTS / "gameover.png")
    more = ""
    for _ in range(100):
        e.run(5)
        more += e.log()
        if "hiscore rank" in more or "sky: title" in more:
            break
    if "hiscore rank" in more:
        e.run(10)
        e.screenshot(SHOTS / "initials.png")
        e.type("BOT")
        e.run(10)
        more += e.log()
        check("high-score name entry", "sky: name=BOT" in more, more[-200:])
        check("back to the title", "sky: title" in more or e.run_until("sky: title", 300))
    else:
        check(f"score {final} below the table: no entry", final < 2000)
        check("back to the title", "sky: title" in more)
    e.run(20)
    e.screenshot(SHOTS / "hiscores.png")
    rec = e.record_input("save", REC)
    tape = e.read_mem(TAPE, TAPE_LEN - struct.unpack("<H", e.read_mem("tick_left", 2))[0])
    print(f"  recorded {rec['frames']} frames, {rec['events']} input changes, final score {final}")

    # two players: keys for player 1, the Kempston for player 2
    e.run(30)
    e.press("2")
    check("key 2 starts a two-player game", e.run_until("sky: state=intro wave=1 players=2", 100))
    e.run_until("sky: state=playing", 200)
    g0 = read_gs(e)
    e.hold("P+LEFT")                         # player 1 right (P), player 2 left (Kempston)
    e.run(30)
    e.release()
    e.run(10)
    g1 = read_gs(e)
    check("player 1 moves right on the keys, player 2 left on the Kempston",
          g1["p"]["x"] > g0["p"]["x"] + 30 and g1["p2"]["x"] < g0["p2"]["x"] - 30,
          f"{g0['p']['x']}->{g1['p']['x']}, {g0['p2']['x']}->{g1['p2']['x']}")
    e.hold("SPACE")
    e.run(20)
    e.release()
    g2 = read_gs(e)
    check("SPACE flaps player 1 only", g2["p"]["y"] < g1["p"]["y"] - 10 and g2["p2"]["y"] >= g1["p2"]["y"] - 2,
          f"{g1['p']['y']}->{g2['p']['y']}, {g1['p2']['y']}->{g2['p2']['y']}")
    e.screenshot(SHOTS / "two_players.png")

seqs = []
for i in range(2):
    with Emulator() as e:
        r = e.replay(REC)
        seqs.append([h for _, h in r["hashes"]])
        check(f"replay {i + 1}: every frame hash matches the recorded game", r["ok"],
              f"{r.get('mismatches')} mismatches, first at {r.get('first_mismatch')}")
check("two replays give identical screens", seqs[0] == seqs[1] and len(seqs[0]) > 1000)

ok, out = build_cref()
check("the C reference build (PHYS_IN_C=1) builds", ok, out)
if ok:
    asm_log, c_log = tape_game(TAP, tape), tape_game(CREF, tape)
    if DEBUG:
        print("ASM\n" + asm_log + "\nC\n" + c_log)
    check("assembly loops play the same game as the original C",
          asm_log == c_log and "gameover" in asm_log,
          f"asm {asm_log.count(chr(10))} lines, C {c_log.count(chr(10))}; first difference: " +
          next((f"{a!r} vs {b!r}" for a, b in zip(asm_log.splitlines(), c_log.splitlines()) if a != b), "-"))
    print(f"  the bot's game from its {len(tape)} ticks of input: {asm_log.count('joust won')} jousts won, "
          f"{asm_log.count('knight down')} lost, {asm_log.count('pod caught')} pods, game over: {'gameover' in asm_log}")

with Emulator(machine="128k") as e:
    e.load(TAP)
    e.run_until("sky: ready", 4000)
    e.run(1)
    check("128K: finds the AY", "ay=1" in e.log(0), e.log(0)[-80:])
    e.break_at("_ay_tone")
    check("128K: the AY plays the melody on the title", e.continue_(30).get("why") == "breakpoint")

print(f"{'FAIL' if failures else 'PASS'}: sky_knights")
sys.exit(1 if failures else 0)
