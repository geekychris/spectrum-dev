"""Jump Quest (48K) - plays the game through the harness.

1. Recorded from reset: left alone, the title starts the demo, the bot: the
   solver's routes (tools/solve.sh, a search over the game's own jq_tick built
   for the host) played inside the game. It must clear level 1, the Office
   Park, with no life lost, while the view follows. SPACE stops it; FIRE then
   starts a real game, which walks right (O/P keys) into the first beetle and
   the first pit until the game is over (each life takes the diskette on the
   way: 300 points), enters BOT in the high scores and is back on the title.
2. The recording is replayed twice: every frame hash must match.
3. The demo again, to the end: all three levels, the CONGRATULATIONS screen,
   the frame rate (the probe counts frames drawn and logic ticks), the GIF.
4. Two players: DOWN on the title, then RIGHT picks Dale for player 1.
5. On a 128K the AY is found and the demo clears level 1 too.
The stack must never reach the game's state at 0xF200 (a canary below it).
"""
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "zxdev" / "src"))

from zxdev.emu import Emulator  # noqa: E402

HERE = Path(__file__).resolve().parent
TAP = HERE / "build" / "jump_quest.tap"
SHOTS = HERE / "screenshots"
REC = HERE / "build" / "bot-game.json"
GIF = ROOT / "docs" / "images" / "jump_quest.gif"
CANARY, CANARY_LEN = 0xFCC0, 0x140
failures = []


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


def probe(e):
    """jq_probe in main.c"""
    v = struct.unpack("<HBBBBhhhHHHH", e.read_mem("jq_probe", 20))
    keys = ("level_ticks", "state", "level", "lives", "health", "x", "y", "cam_x",
            "frames", "ticks", "overruns", "views")
    return dict(zip(keys, v))


def shot(e, name):
    """a screenshot of a whole frame: at the next frame_wait (the loop's
    drawing is done), one frame later (the screen is rendered at its end)"""
    e.break_at("_frame_wait")
    e.continue_(30)
    e.delete()
    e.run(1)
    e.screenshot(SHOTS / name)


def canary_ok(e):
    return e.read_mem(CANARY, CANARY_LEN) == bytes([0xA5]) * CANARY_LEN


def perf(log):
    """(fps, logic ticks per second) from the perf lines, and per level"""
    m = [tuple(map(int, x)) for x in
         re.findall(r"perf frames=(\d+) ticks=(\d+) overruns=(\d+) scrolls=(\d+) level=(\d+)", log)]
    m = list(dict.fromkeys(m))
    if len(m) < 2:
        return None
    per = {}
    for (f0, t0, _, _, l0), (f1, t1, _, _, l1) in zip(m, m[1:]):
        if l0 == l1 and t1 > t0:
            f, t = per.get(l1, (0, 0))
            per[l1] = (f + f1 - f0, t + t1 - t0)
    f = sum(a for a, _ in per.values())
    t = sum(b for _, b in per.values())
    if not t:
        return None
    return 50.0 * f / t, {lv: 50.0 * a / b for lv, (a, b) in sorted(per.items())}


# ------------------------------------------------------------ 1. the bot, then a real game (recorded)
with Emulator() as e:
    e.load(TAP)
    check("boots to the title", e.run_until("jq: ready", 3000))
    e.run(80)
    SHOTS.mkdir(exist_ok=True)
    shot(e, "title.png")
    e.record_input("start", from_reset=True)
    e.run_until("jq: title", 3000)
    check("the title starts the demo (the bot) after 12 s", e.run_until("jq: demo start", 700))
    log = ""
    t0 = e.position()["since_load"]
    for _ in range(80):
        e.run(25)
        log += e.log()
        if "cleared level=1" in log or "died" in log:
            break
        if "play.png" not in [p.name for p in SHOTS.iterdir()] and e.position()["since_load"] - t0 > 500:
            shot(e, "play.png")
    p = probe(e)
    check("the bot clears level 1, the Office Park", "cleared level=1" in log,
          "; ".join(x for x in log.splitlines() if "jq:" in x)[-300:])
    check("... without losing a life", "died" not in log)
    check("the view follows the player along the level (jumps)", p["views"] >= 8 and p["x"] >= 1500, str(p))
    m = re.search(r"cleared level=1 score=(\d+)", log)
    check("level 1 scores (items, ? blocks, stomps)", m and int(m.group(1)) >= 200, m and m.group(0))
    e.run(30)
    shot(e, "cleared.png")
    e.press("SPACE")
    check("SPACE stops the demo", e.run_until("jq: title", 100))
    e.run(30)
    e.press("SPACE", hold_frames=1, release_frames=0)
    check("FIRE starts a game as RJ, one player", e.run_until("jq: start character=0 players=1 demo=0", 300))

    # walk right into the first beetle and the first pit, three times
    more = ""
    for life in range(3):
        e.hold("P")
        for _ in range(300):
            e.run(4)
            more += e.log()
            if more.count("died") > life:
                break
        e.release()
        e.run(100)
        more += e.log()
        if "gameover" in more:
            break
    log = more
    lives = [int(v) for v in re.findall(r"died lives=(-?\d+)", log)]
    check("a life is lost (the beetle, then the pit)", lives[:1] == [2] and "health=2" in log, str(lives))
    check("three lives, then game over", lives == [2, 1, 0] and "jq: gameover" in log, str(lives))
    m = re.search(r"gameover score=(\d+)", log)
    final = int(m.group(1)) if m else 0
    check("the score is kept from life to life (the diskette each time: 300)", final == 300, str(final))
    e.run(20)
    shot(e, "gameover.png")
    e.run(140)                              # the game over screen waits 150 ticks for FIRE
    e.press("SPACE")
    end = ""
    for _ in range(60):
        e.run(5)
        end += e.log()
        if "hiscore rank" in end:
            break
    check("a high score asks for initials", "jq: hiscore rank=" in end, end[-200:])
    e.run(10)
    e.screenshot(SHOTS / "initials.png")
    e.type("BOT")
    check("initials entered", e.run_until("jq: name=BOT", 100))
    check("back to the title", e.run_until("jq: title", 200))
    e.run(30)
    e.screenshot(SHOTS / "hiscores.png")
    check("the stack stayed clear of the game state", canary_ok(e))
    rec = e.record_input("save", REC)
    print(f"  recorded {rec['frames']} frames, {rec['events']} input changes, final score {final}")

# ------------------------------------------------------------ 2. replays
seqs = []
for i in range(2):
    with Emulator() as e:
        r = e.replay(REC)
        seqs.append([h for _, h in r["hashes"]])
        check(f"replay {i + 1}: every frame hash matches the recorded game", r["ok"],
              f"{r.get('mismatches')} mismatches, first at {r.get('first_mismatch')}")
check("two replays give identical screens", seqs[0] == seqs[1] and len(seqs[0]) > 1000)

# ------------------------------------------------------------ 3. the demo: all three levels
with Emulator() as e:
    e.load(TAP)
    e.run_until("jq: demo start", 3000)
    e.run(300)
    log = e.log()
    GIF.parent.mkdir(parents=True, exist_ok=True)
    e.record_video(GIF, frames=300, every=2, scale=1)
    log += e.log()
    for _ in range(80):
        if "gameover" in log or "died" in log:
            break
        e.run(100)
        log = e.log_text(0)
        log += e.log()
        for lv in (2, 3):
            if f"state=playing level={lv}" in log and not (SHOTS / f"level{lv}.png").exists():
                e.run(300)
                shot(e, f"level{lv}.png")
                log += e.log()
    if "jq: gameover score" not in e.log_text(0) and "died" not in log:
        e.run_until("jq: gameover score", 3000)   # (after the state line, the last bonus pause)
        e.run(5)
    log = e.log_text(0)                     # (all of it: shot() runs past some lines)
    cleared = list(dict.fromkeys(re.findall(r"cleared level=(\d+)", log)))
    check("the demo clears all three levels", cleared == ["1", "2", "3"], str(cleared))
    check("no life lost on the way", "died" not in log)
    m = re.search(r"jq: gameover .*", log)
    check("the end: CONGRATULATIONS", "won=1" in log, (m and m.group(0)) or log[-400:])
    fps = perf(log)
    if fps:
        p = probe(e)
        print(f"  speed: {fps[0]:.1f} fps drawn, logic 50 ticks/s; per level: "
              + ", ".join(f"{lv}: {v:.1f}" for lv, v in fps[1].items()))
        check("draws at least 6 frames a second", fps[0] >= 6, f"{fps[0]:.1f}")
    else:
        check("perf lines logged", False)
    e.run(20)
    shot(e, "won.png")
    check("back to the title after the demo", e.run_until("jq: title", 900))
    check("the stack stayed clear of the game state (demo)", canary_ok(e))

# ------------------------------------------------------------ 3b. logic at 50 Hz
with Emulator() as e:
    e.load(TAP)
    e.run_until("jq: demo start", 3000)
    e.run(60)
    a, f0 = probe(e), e.position()["since_load"]
    e.run(500)
    b, f1 = probe(e), e.position()["since_load"]
    hz = 50.0 * (b["ticks"] - a["ticks"]) / (f1 - f0)
    fps = 50.0 * (b["frames"] - a["frames"]) / (f1 - f0)
    print(f"  level 1, 500 frames: logic {hz:.1f} ticks/s, {fps:.1f} frames drawn/s")
    check("the logic keeps 50 ticks a second (within the loop's last frames)", hz >= 48.5, f"{hz:.1f}")

# ------------------------------------------------------------ 4. two players, Dale
with Emulator() as e:
    e.load(TAP)
    e.run_until("jq: title", 3000)
    e.run(20)
    e.press("A")                            # DOWN: 2 players
    e.press("P")                            # RIGHT: Dale
    e.run(10)
    e.press("SPACE", hold_frames=1, release_frames=0)
    check("two players, Dale first", e.run_until("jq: start character=1 players=2 demo=0", 300))

# ------------------------------------------------------------ 5. 128K: the AY
with Emulator() as e:
    e.load(TAP, machine="128k")
    check("128K: AY found", e.run_until("ay=1", 3000))
    check("128K: the demo starts", e.run_until("jq: demo start", 1000))
    check("128K: the demo clears level 1", e.run_until("cleared level=1", 2500))

print(f"{'FAIL' if failures else 'PASS'}: jump_quest")
sys.exit(1 if failures else 0)
