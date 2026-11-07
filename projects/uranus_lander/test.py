"""Uranus Lander (48K/128K) - plays a whole game through the harness.

Boot -> title -> start -> the game's own autopilot (Z) lands on levels 1 and
2 -> autopilot off on level 3: the ship falls and crashes, three times ->
game over -> fire -> initials (BOT) -> back to the title with BOT in the
table. Terrain and pads are checked against the original's generator (in
Python below), the logic against the 50 Hz clock, and the frame rate is
reported. The game is recorded and replayed twice: every frame hash must
match. Then a short 128K run checks the AY. Screenshots go to
projects/uranus_lander/screenshots/.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "zxdev" / "src"))

from zxdev.emu import Emulator  # noqa: E402

HERE = Path(__file__).resolve().parent
TAP = HERE / "build" / "uranus_lander.tap"
SHOTS = HERE / "screenshots"
REC = HERE / "build" / "bot-game.json"
failures = []


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


# ---------------------------------------------------------------- the original's terrain

def original_level(level):
    """generate_terrain() of amiga_mcp/examples/uranus_lander/game.c (32-bit int)."""
    state = (level * 31337 + 54321) & 0xFFFFFFFF

    def rng():
        nonlocal state
        state = (state * 1103515245 + 12345) & 0xFFFFFFFF
        return (state >> 16) & 0x7FFF

    def cdiv(a, b):                          # C division truncates
        return int(a / b)

    rough = min(2 + level, 8)
    y, t = 200, []
    for _ in range(320):
        y += rng() % (rough * 2 + 1) - rough
        if y < 130:
            y = 135
        if y > 238:
            y = 233
        t.append(y)
    mult_table = [[1, 1, 2, 2], [1, 2, 2, 3], [2, 2, 3, 5], [2, 3, 5, 5]]
    widths = {1: 40, 2: 30, 3: 22, 5: 14}
    diff = min((level - 1) // 2, 3)
    pads = []
    for i in range(4):
        mult = mult_table[diff][i]
        w = widths[mult]
        px = i * 80 + 10 + rng() % (80 - w - 20)
        px = max(px, 5)
        if px + w >= 315:
            px = 320 - w - 5
        py = min(max(t[px + w // 2], 140), 233)
        for x in range(px, min(px + w, 320)):
            t[x] = py
        if px > 2:
            d = t[px - 1] - py
            if d > 3 or d < -3:
                t[px - 1] = py + cdiv(d, 2)
        if px + w < 318:
            d = t[px + w] - py
            if d > 3 or d < -3:
                t[px + w] = py + cdiv(d, 2)
        pads.append((px, w, py, mult))
    checksum = sum(v * (i + 1) for i, v in enumerate(t)) & 0xFFFF
    return checksum, pads


def logged_levels(log):
    out = {}
    for m in re.finditer(r"uranus: level=(\d+) terrain=(\d+) pads=([\d,;]+)", log):
        pads = [tuple(map(int, p.split(","))) for p in m.group(3).split(";") if p]
        out[int(m.group(1))] = (int(m.group(2)), pads)
    return out


# ---------------------------------------------------------------- helpers

def settle(e):
    """stop where the main loop waits for the next frame: the screen is whole"""
    e.break_at("_frame_wait")
    e.continue_(10)
    e.delete()


def shot(e, name):
    settle(e)
    return e.screenshot(SHOTS / f"{name}.png")


def until(e, text, frames, log):
    ok = e.run_until(text, frames)
    log.append(e.log())
    return ok


# ---------------------------------------------------------------- the game (48K)

with Emulator() as e:
    e.load(TAP)
    check("boots to the title", e.run_until("uranus: ready", 3000))
    e.run(60)
    title = shot(e, "title")
    check("title screen drawn (planet, name, table)", len(title.getcolors(1 << 16)) >= 6)

    e.record_input("start", from_reset=True)       # the whole game, for the replay check
    logs = []
    until(e, "uranus: ready", 3000, logs)
    e.run(40)
    e.press("SPACE")
    check("FIRE starts the game", until(e, "uranus: level=1", 200, logs))
    e.run(20)
    e.press("Z")
    check("Z switches the autopilot on", until(e, "uranus: autopilot=1", 20, logs))
    check("the autopilot lands on level 1", until(e, "uranus: landed level=1", 3000, logs))
    e.run(25)
    shot(e, "landed")
    check("the autopilot lands on level 2", until(e, "uranus: landed level=2", 3500, logs))
    check("level 3 starts", until(e, "uranus: level=3", 300, logs))
    e.run(150)
    shot(e, "play")
    e.press("Z")                                    # autopilot off: the ship falls
    check("autopilot off", until(e, "uranus: autopilot=0", 20, logs))
    check("a crash", until(e, "uranus: crash level=3", 1500, logs))
    e.run(12)
    shot(e, "crash")
    check("game over after three crashes", until(e, "state=GAMEOVER", 2500, logs))
    e.run(70)
    shot(e, "gameover")
    e.press("SPACE")
    check("FIRE: high-score entry", until(e, "uranus: hiscore rank=0", 100, logs))
    e.run(2)
    e.screenshot(SHOTS / "hiscore.png")             # (no breakpoint: typing at once, latched)
    e.type("BOT")
    check("initials entered", until(e, "uranus: name=BOT", 60, logs))
    check("back to the title", until(e, "uranus: title", 60, logs))
    e.run(30)
    shot(e, "hiscores")
    log = e.log(0)

    # rules and numbers
    lv = logged_levels(log)
    for n in (1, 2, 3):
        exp = original_level(n)
        check(f"level {n} terrain and pads are the original's", lv.get(n) == exp,
              f"{lv.get(n)} vs {exp}")
    landed = [(int(a), int(b), int(c), int(d)) for a, b, c, d in
              re.findall(r"landed level=(\d+) pad=\d+ x(\d+) bonus=(\d+) score=(\d+)", log)]
    check("landing bonus = multiplier * 50 + fuel left", all(
        b > m * 50 and b - m * 50 <= 600 for _, m, b, _ in landed) and len(landed) == 2, str(landed))
    check("the bonus is added to the score", len(landed) == 2 and landed[1][3] == landed[0][2] + landed[1][2])
    crashes = re.findall(r"uranus: crash level=3 score=(\d+) lives=(\d+)", log)
    check("three lives lost", [c[1] for c in crashes] == ["3", "2", "1"], str(crashes))
    m = re.search(r"gameover score=(\d+)", log)
    final = int(m.group(1)) if m else 0
    check("final score kept", landed and final == landed[-1][3], f"{final}")
    check("level 2 is harder (gravity, fuel)", "gravity=1150 fuel=570" in log)

    # speed: the logic runs once per 50 Hz frame however long the drawing takes.
    # Measured over the 250-frame windows of flight with no screen change in them
    # (a new level's screen takes ~0.5 s to draw; the game waits for it).
    perf, changed = [], False
    for line in log.splitlines():
        if "uranus: state=" in line:
            changed = True
        m = re.search(r"perf draws=(\d+) frames=(\d+) ticks=(\d+) state=PLAYING", line)
        if m:
            if not changed:
                perf.append(tuple(map(int, m.groups())))
            changed = False
    if perf:
        draws = sum(p[0] for p in perf)
        frames = sum(p[1] for p in perf)
        ticks = sum(p[2] for p in perf)
        fps = 50 * draws / frames
        print(f"  frame rate while flying: {fps:.1f} fps ({draws} screens in {frames} frames), "
              f"logic {50 * ticks / frames:.1f} Hz")
        check("logic at 50 Hz (a tick per frame)", ticks >= frames * 0.98, f"{ticks}/{frames}")
        check("frame rate >= 20 fps", fps >= 20, f"{fps:.1f}")
    else:
        check("perf lines logged", False)

    hs = e.screenshot()
    check("BOT in the high-score table", "name=BOT" in log and len(hs.getcolors(1 << 16)) >= 5)
    rec = e.record_input("save", REC)
    print(f"  recorded {rec['frames']} frames, {rec['events']} input changes")

# ---------------------------------------------------------------- replays

seqs = []
for i in range(2):
    with Emulator() as e:
        r = e.replay(REC)
        seqs.append([h for _, h in r["hashes"]])
        check(f"replay {i + 1}: every frame hash matches the recorded game", r["ok"],
              f"{r.get('mismatches')} mismatches, first at {r.get('first_mismatch')}")
check("two replays give identical screens", seqs[0] == seqs[1] and len(seqs[0]) > 2000)

# ---------------------------------------------------------------- 128K: the AY

with Emulator(machine="128k") as e:
    e.load(TAP)
    check("128K: boots, AY found", e.run_until("uranus: ready kempston=1 ay=1", 3000))
    e.run(40)
    e.press("SPACE")
    e.run_until("uranus: level=1", 200)
    e.run(20)
    e.press("Z")
    noise = False
    for _ in range(60):                             # the thrust rumble: noise on channel A
        e.run(5)
        if not e.read_mem("_ay_mixer", 1)[0] & 0x08:
            noise = True
            break
    check("128K: thrust sounds on the AY (noise, channel A)", noise)
    check("128K: the autopilot lands", e.run_until("uranus: landed level=1", 3000))

print(f"{'FAIL' if failures else 'PASS'}: uranus_lander")
sys.exit(1 if failures else 0)
