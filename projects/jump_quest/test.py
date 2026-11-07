"""Jump Quest (48K) - plays the game through the harness.

1. A bot follows the solver's route (tools/routes.json, found by tools/solve.sh
   over the game's own jq_tick) through level 1, the Office Park, holding the
   keys it needs tick by tick (it reads the game's tick counter from
   `jq_probe`). In level 2 it walks right into the enemies and the first pit
   until the game is over, enters BOT in the high scores and is back on the
   title. The game is recorded.
2. The recording is replayed twice: every frame hash must match.
3. Left alone, the title starts the demo, which plays all three levels to
   the end (the routes again, inside the game); the GIF is taken from it.
4. On a 128K the AY is found and the route clears level 1 too.
Speed: the probe counts frames drawn and logic ticks (fps = 50 * frames / ticks).
"""
import json
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
ROUTES = json.loads((HERE / "tools" / "routes.json").read_text())["routes"]
INP_LEFT, INP_RIGHT, INP_JUMP = 0x01, 0x02, 0x10
STATE_PLAYING = 1
failures = []


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


def probe(e):
    """jq_probe in main.c"""
    b = e.read_mem("jq_probe", 20)
    lt, state, level, lives, health, x, y, cam, frames, ticks, over, scrolls = struct.unpack("<HBBBBhhhHHHH", b)
    return dict(level_ticks=lt, state=state, level=level, lives=lives, health=health, x=x, y=y,
                cam_x=cam, frames=frames, ticks=ticks, overruns=over, scrolls=scrolls)


def expand(route):
    out = []
    for flags, n in route:
        out += [flags] * n
    return out


def keys_for(flags):
    k = []
    if flags & INP_LEFT:
        k.append("O")
    if flags & INP_RIGHT:
        k.append("P")
    if flags & INP_JUMP:
        k.append("Q")
    return "+".join(k)


def set_keys(e, flags, held):
    if flags != held:
        e.release()
        if flags:
            e.hold(keys_for(flags))
    return flags


def follow_route(e, level, max_ticks=3000):
    """play level `level` (0-based) by its route; True when it is cleared.
    Stops at every jq_tick() (the keys for this tick were read just before):
    the keys for the next tick are set there, so the interrupt's latch sees
    exactly the presses of the route even when a slow frame runs two ticks."""
    route = expand(ROUTES[level])
    held = set_keys(e, route[0], None)   # the first tick reads them on its own
    log = ""
    e.break_at("_jq_tick")
    try:
        for _ in range(max_ticks):
            if not e.continue_(50)["stopped"]:
                continue                     # not at a tick: its keys may not be read yet
            p = probe(e)
            if p["state"] == STATE_PLAYING and p["level"] == level:
                k = p["level_ticks"] + 1     # this tick reads route[k - 1]
                held = set_keys(e, route[k] if k < len(route) else 0, held)
            log += e.log()
            if f"cleared level={level + 1}" in log or "died" in log:
                break
    finally:
        e.delete()
    set_keys(e, 0, held)
    return f"cleared level={level + 1}" in log, log


def perf(log):
    """(fps, overruns, scrolls) from the last perf line, and fps per level"""
    m = [tuple(map(int, x)) for x in
         re.findall(r"perf frames=(\d+) ticks=(\d+) overruns=(\d+) scrolls=(\d+) level=(\d+)", log)]
    m = list(dict.fromkeys(m))
    if not m:
        return None
    per = {}
    for (f0, t0, _, _, l0), (f1, t1, _, _, l1) in zip(m, m[1:]):
        if l0 == l1 and t1 > t0:
            f, t = per.get(l1, (0, 0))
            per[l1] = (f + f1 - f0, t + t1 - t0)
    f, t, o, s, _ = m[-1]
    return 50.0 * f / t, o, s, {lv: 50.0 * a / b for lv, (a, b) in sorted(per.items())}


# ------------------------------------------------------------ 1. the bot's game (recorded)
with Emulator() as e:
    e.load(TAP)
    check("boots to the title", e.run_until("jq: ready", 3000))
    e.run(80)
    SHOTS.mkdir(exist_ok=True)
    e.screenshot(SHOTS / "title.png")
    e.record_input("start", from_reset=True)
    e.run_until("jq: title", 3000)
    e.run(30)
    e.press("SPACE", hold_frames=1, release_frames=0)
    ok, log = follow_route(e, 0)
    check("FIRE starts level 1 as RJ", "state=playing level=1 player=1 lives=3 health=3 score=0" in log, log[:200])
    check("the bot clears level 1, the Office Park", ok,
          "; ".join(x for x in log.splitlines() if "jq:" in x)[-300:])
    p = probe(e)
    check("the view scrolled along with the player", p["scrolls"] >= 60 and p["cam_x"] >= 1600 - 256 - 16,
          str(p))
    e.run(20)
    e.screenshot(SHOTS / "cleared.png")
    log += e.log()
    check("level 2 starts after the bonus", e.run_until("state=playing level=2", 300))
    e.run(40)
    log += e.log()
    m = re.search(r"cleared level=1 score=(\d+)", log)
    score1 = int(m.group(1)) if m else 0
    check("level 1 scores (items, ? blocks)", score1 >= 200, str(score1))

    # level 2, the Server Room: hold right, past the beetle into the first pit
    more = ""
    for life in range(3):
        e.hold("P")
        for _ in range(600):
            e.run(2)
            more += e.log()
            if more.count("died") > life:
                break
            if life == 0 and _ == 60 and not (SHOTS / "play.png").exists():
                e.screenshot(SHOTS / "play.png")
        e.release()
        e.run(60)
        more += e.log()
        if "gameover" in more:
            break
    log += more
    lives = [int(v) for v in re.findall(r"died lives=(-?\d+)", log)]
    check("a life is lost in level 2", lives[:1] == [2], str(lives))
    check("three lives, then game over", lives == [2, 1, 0] and "jq: gameover" in log, str(lives))
    m = re.search(r"gameover score=(\d+)", log)
    final = int(m.group(1)) if m else 0
    check("the score is kept from level to level and life to life", final >= score1 + 500, f"{final} {score1}")
    e.run(30)
    e.screenshot(SHOTS / "gameover.png")
    e.run(130)                              # the game over screen waits 150 ticks for FIRE
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
check("two replays give identical screens", seqs[0] == seqs[1] and len(seqs[0]) > 500)

# ------------------------------------------------------------ 3. the demo: all three levels
with Emulator() as e:
    e.load(TAP)
    e.run_until("jq: title", 3000)
    check("the title starts the demo after 12 s", e.run_until("jq: demo start", 700))
    e.run(200)
    log = e.log()
    GIF.parent.mkdir(parents=True, exist_ok=True)
    e.record_video(GIF, frames=400, every=2, scale=1)
    log += e.log()
    for _ in range(60):
        if "gameover" in log or "died" in log:
            break
        e.run(100)
        log += e.log()
        for lv in (2, 3):
            if f"state=playing level={lv}" in log and not (SHOTS / f"level{lv}.png").exists():
                e.run(150)
                e.screenshot(SHOTS / f"level{lv}.png")
    cleared = list(dict.fromkeys(re.findall(r"cleared level=(\d+)", log)))
    check("the demo clears all three levels", cleared == ["1", "2", "3"], str(cleared))
    check("no life lost on the way", "died" not in log)
    check("the end: CONGRATULATIONS", "gameover score=" in log and "won=1" in log)
    fps = perf(log)
    if fps:
        print(f"  speed: {fps[0]:.1f} fps drawn, logic 50 ticks/s, {fps[1]} overrun frames, {fps[2]} scrolls")
        print("  per level: " + ", ".join(f"{lv}: {v:.1f}" for lv, v in fps[3].items()))
        check("draws at least 25 frames a second", fps[0] >= 25, f"{fps[0]:.1f}")
    else:
        check("perf lines logged", False)
    e.run(20)
    e.screenshot(SHOTS / "won.png")
    check("back to the title after the demo", e.run_until("jq: title", 900))

# ------------------------------------------------------------ 4. 128K: the AY
with Emulator() as e:
    e.load(TAP, machine="128k")
    check("128K: AY found", e.run_until("ay=1", 3000))
    e.run_until("jq: title", 300)
    e.run(30)
    e.press("SPACE", hold_frames=1, release_frames=0)
    ok, log = follow_route(e, 0)
    check("128K: the game starts", "state=playing level=1" in log)
    check("128K: the route clears level 1 too", ok)

print(f"{'FAIL' if failures else 'PASS'}: jump_quest")
sys.exit(1 if failures else 0)
