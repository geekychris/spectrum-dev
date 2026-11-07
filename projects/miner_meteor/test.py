"""Miner Meteor (48K) - plays the game through the harness.

1. A bot follows the solver's route (tools/routes.json, found by tools/solve.sh
   over the game's own game_tick) through cavern 1, holding the keys it needs
   tick by tick (it reads the game's tick counter from `miner_probe`). In
   cavern 2 it walks into the spikes until the game is over, enters BOT in
   the high scores and is back on the title. The game is recorded.
2. The recording is replayed twice: every frame hash must match.
3. Left alone, the title starts the demo, which plays all six caverns to the
   win screen (the routes again, inside the game); the GIF is taken from it.
4. On a 128K the AY is found and the game runs.
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
TAP = HERE / "build" / "miner_meteor.tap"
SHOTS = HERE / "screenshots"
REC = HERE / "build" / "bot-game.json"
GIF = ROOT / "docs" / "images" / "miner_meteor.gif"
ROUTES = json.loads((HERE / "tools" / "routes.json").read_text())
INPUT_LEFT, INPUT_RIGHT, INPUT_JUMP = 1, 2, 4
failures = []


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


def probe(e):
    """miner_probe in main.c"""
    b = e.read_mem("miner_probe", 16)
    lt, mode, level, lives, keys, x, y, frames, ticks, over = struct.unpack("<HBBBBhhHHH", b)
    return dict(level_ticks=lt, mode=mode, level=level, lives=lives, keys=keys, x=x, y=y,
                frames=frames, ticks=ticks, overruns=over)


def expand(route):
    out = []
    for flags, n in route:
        out += [flags] * n
    return out


def keys_for(flags):
    k = []
    if flags & INPUT_LEFT:
        k.append("O")
    if flags & INPUT_RIGHT:
        k.append("P")
    if flags & INPUT_JUMP:
        k.append("Q")
    return "+".join(k)


def set_keys(e, flags, held):
    if flags != held:
        e.release()
        if flags:
            e.hold(keys_for(flags))
    return flags


def follow_route(e, level, max_ticks=3000):
    """play cavern `level` (0-based) by its route; True when it is cleared.
    Stops at every game_tick() (the keys for this tick were read just before):
    the keys for the next tick are set there, so the interrupt's latch sees
    exactly the presses of the route even when a slow frame runs two ticks."""
    route = expand(ROUTES[level])
    held = set_keys(e, route[0], None)   # the first tick reads them on its own
    log = ""
    e.break_at("_game_tick")
    try:
        for _ in range(max_ticks):
            e.continue_(10)
            p = probe(e)
            if p["mode"] == 1 and p["level"] == level:
                k = p["level_ticks"] + 1     # this tick has read route[k - 1]
                held = set_keys(e, route[k] if k < len(route) else 0, held)
            log += e.log()
            if f"cleared level={level + 1}" in log or "died" in log:
                break
    finally:
        e.delete()
    set_keys(e, 0, held)
    return f"cleared level={level + 1}" in log, log


def fps_of(log):
    """overall fps and overruns, and fps per cavern (between perf lines in it)"""
    m = [tuple(map(int, x)) for x in re.findall(r"perf frames=(\d+) ticks=(\d+) overruns=(\d+) level=(\d+)", log)]
    m = list(dict.fromkeys(m))
    if not m:
        return None
    per = {}
    for (f0, t0, _, l0), (f1, t1, _, l1) in zip(m, m[1:]):
        if l0 == l1:
            f, t = per.get(l1, (0, 0))
            per[l1] = (f + f1 - f0, t + t1 - t0)
    f, t, o, _ = m[-1]
    return 50.0 * f / t, o, {lv: 50.0 * a / b for lv, (a, b) in sorted(per.items())}


# ------------------------------------------------------------ 1. the bot's game (recorded)
with Emulator() as e:
    e.load(TAP)
    check("boots to the title", e.run_until("miner: ready", 3000))
    e.run(80)
    e.screenshot(SHOTS / "title.png")
    e.record_input("start", from_reset=True)
    e.run_until("miner: title", 3000)
    e.run(30)
    e.press("SPACE", hold_frames=1, release_frames=0)
    ok, l = follow_route(e, 0)
    log = l
    log += l
    check("FIRE starts cavern 1", "state=playing level=1 lives=3 keys=4" in log, log[:200])
    check("the bot clears cavern 1 (four keys, then the exit)", ok,
          "; ".join(x for x in l.splitlines() if "miner:" in x)[-300:])
    check("keys are collected one by one", all(f"keys={k} " in log for k in (3, 2, 1, 0)))
    e.run(20)
    e.screenshot(SHOTS / "cleared.png")
    e.run_until("state=playing level=2", 200)
    e.run(60)
    e.screenshot(SHOTS / "play.png")

    # cavern 2, Hazard Bay: walk right into the floor spikes until the game is over
    more = ""
    for life in range(3):
        e.hold("P")
        for _ in range(200):
            e.run(1)
            more += e.log()
            if more.count("died") > life:
                break
        e.release()
        e.run(40)
        more += e.log()
        if "gameover" in more:
            break
    log += more
    lives = [int(v) for v in re.findall(r"died lives=(\d+)", log)]
    check("a life is lost on the spikes", lives[:1] == [2], str(lives))
    check("three lives, then game over", lives == [2, 1, 0] and "miner: gameover" in log, str(lives))
    m = re.search(r"gameover score=(\d+)", log)
    final = int(m.group(1)) if m else 0
    check("score: 4 keys + the cavern + half the air left", 1400 <= final <= 1900, str(final))
    e.run(30)
    e.screenshot(SHOTS / "gameover.png")
    e.run(130)                              # the end screen waits 150 ticks for FIRE
    e.press("SPACE")
    end = ""
    for _ in range(60):
        e.run(5)
        end += e.log()
        if "hiscore rank" in end:
            break
    check("a high score asks for initials", "miner: hiscore rank=" in end, end[-200:])
    e.run(10)
    e.screenshot(SHOTS / "initials.png")
    e.type("BOT")
    check("initials entered", e.run_until("miner: name=BOT", 100))
    check("back to the title", e.run_until("miner: title", 200))
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

# ------------------------------------------------------------ 3. the demo: all six caverns
with Emulator() as e:
    e.load(TAP)
    e.run_until("miner: title", 3000)
    check("the title starts the demo after 12 s", e.run_until("miner: demo start", 700))
    e.run(100)
    e.screenshot(SHOTS / "demo.png")
    log = e.log()
    GIF.parent.mkdir(parents=True, exist_ok=True)
    e.record_video(GIF, frames=400, every=2, scale=1)
    log += e.log()
    for _ in range(40):
        if "state=won" in log or "died" in log:
            break
        e.run(100)
        log += e.log()
        if "state=playing level=6" in log and not (SHOTS / "cavern6.png").exists():
            e.run(60)
            e.screenshot(SHOTS / "cavern6.png")
    cleared = list(dict.fromkeys(re.findall(r"cleared level=(\d+)", log)))
    check("the demo clears all six caverns", cleared == ["1", "2", "3", "4", "5", "6"], str(cleared))
    check("no life lost on the way", "died" not in log)
    check("the win screen", "state=won" in log)
    fps = fps_of(log)
    if fps:
        print(f"  speed: {fps[0]:.1f} fps drawn, logic 50 ticks/s, {fps[1]} overrun frames in the demo")
        print("  per cavern: " + ", ".join(f"{lv}: {v:.1f}" for lv, v in fps[2].items()))
        check("draws at least 45 frames a second", fps[0] >= 45, f"{fps[0]:.1f}")
    else:
        check("perf lines logged", False)
    e.run(20)
    e.screenshot(SHOTS / "won.png")
    check("back to the title after the demo", e.run_until("miner: title", 600))

# ------------------------------------------------------------ 4. 128K: the AY
with Emulator() as e:
    e.load(TAP, machine="128k")
    check("128K: AY found", e.run_until("ay=1", 3000))
    e.run_until("miner: title", 300)
    e.run(30)
    e.press("SPACE", hold_frames=1, release_frames=0)
    ok, l = follow_route(e, 0)
    check("128K: the game starts", "state=playing level=1" in l)
    check("128K: the route clears cavern 1 too", ok)

print(f"{'FAIL' if failures else 'PASS'}: miner_meteor")
sys.exit(1 if failures else 0)
