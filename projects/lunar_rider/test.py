"""Lunar Rider (Spectrum Next) - plays a whole game through the harness.

Boot -> title -> start -> a bot rides: it holds A (shoot forward and up)
and jumps (Q) when a crater, rock or mine is just ahead, reading the game's
probe (lunar_probe: the terrain ahead of the buggy) -> it passes a
checkpoint and scores, loses its lives (craters it can't clear, bombs) ->
game over -> fire -> initials (BOT) -> back to the title with BOT in the
table. Checked: the logic at 50 Hz, the frame rate, the bands' hardware
scroll, the music and effects on the TurboSound. The whole game is recorded
and replayed twice: every frame hash must match. Screenshots go to
projects/lunar_rider/screenshots/.
"""
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "zxdev" / "src"))

from zxdev.emu import Emulator  # noqa: E402

HERE = Path(__file__).resolve().parent
NEX = HERE / "build" / "lunar_rider.nex"
SHOTS = HERE / "screenshots"
REC = HERE / "build" / "bot-game.json"
failures = []

CRATER_SM, CRATER_LG, ROCK, MINE = 1, 2, 3, 5
BUGGY_W = 24


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


def probe(e):
    b = e.read_mem("_lunar_probe", 12 + 112)
    gx, y, vy, on_ground, alive, state, lives = struct.unpack("<IhhBBBB", b[:12])
    return dict(gx=gx, y=y, vy=vy, on_ground=on_ground, alive=alive, state=state, lives=lives, ahead=b[12:])


def danger(p, look=20):
    """a crater under the buggy's centre, or a rock / mine at its front, within `look` pixels"""
    a = p["ahead"]
    for k in range(look):
        if a[BUGGY_W // 2 + k] in (CRATER_SM, CRATER_LG) or a[BUGGY_W + k] in (ROCK, MINE):
            return True
    return False


def bot_game(e, max_frames=12000):
    """ride until the game is over; returns the log"""
    log, held, t = "", None, 0
    shot_play = shot_cp = False
    while t < max_frames:
        p = probe(e)
        if p["state"] == 3:                          # GAMEOVER
            break
        jump = p["state"] in (1, 4) and p["on_ground"] and danger(p)
        keys = "A+Q" if jump else "A"
        if keys != held:
            e.release()
            e.hold(keys)
            held = keys
        e.run(1)
        t += 1
        log += e.log()
        if not shot_play and t > 300 and p["state"] == 1 and p["alive"]:
            shot(e, "play")
            shot_play = True
        if not shot_cp and "lunar: checkpoint" in log:
            e.run(4)
            shot(e, "checkpoint")
            shot_cp = True
    e.release()
    return log


def settle(e):
    """the frame drawn: stop where the main loop waits for the next frame, then
    let the emulator finish that frame (a screenshot taken at the breakpoint
    shows the rows below the beam from the frame before)"""
    e.break_at("_frame_wait")
    e.continue_(10)
    e.delete()
    e.run(1)


def shot(e, name):
    settle(e)
    return e.screenshot(SHOTS / f"{name}.png")


with Emulator(machine="next") as e:
    e.load(NEX)
    check("boots to the title", e.run_until("lunar: ready", 600))
    e.run(60)
    title = shot(e, "title")
    check("title screen drawn", len(title.getcolors(1 << 16)) >= 6)

    e.record_input("start", from_reset=True)        # the whole game, for the replay check
    e.run_until("lunar: ready", 600)
    e.run(40)
    e.press("SPACE")
    check("FIRE / SPACE starts the game", e.run_until("lunar: state=PLAYING", 60))
    log = bot_game(e)
    log = e.log(0)

    scores = [int(s) for s in re.findall(r"lunar: score=(\d+)", log)]
    cps = re.findall(r"lunar: checkpoint zone=(\d+) score=(\d+)", log)
    deaths = re.findall(r"lunar: death lives=(\d+)", log)
    check("the bot rides past a checkpoint (+1000)", len(cps) >= 1, str(cps))
    check("the bot scores", scores and max(scores) >= 1000, str(scores[-4:]))
    check("lives are lost", len(deaths) == 3, str(deaths))
    m = re.search(r"gameover score=(\d+)", log)
    check("game over", m is not None)
    final = int(m.group(1)) if m else 0
    e.run(70)
    shot(e, "gameover")

    # speed: the logic once per 50 Hz frame, the screen drawn every frame while riding
    perf, changed = [], False
    for line in log.splitlines():
        if "lunar: state=" in line:
            changed = True
        mm = re.search(r"perf draws=(\d+) frames=(\d+) ticks=(\d+) lines=(\d+) worst=(\d+) state=PLAYING", line)
        if mm:
            if not changed:
                perf.append(tuple(map(int, mm.groups())))
            changed = False
    if perf:
        draws, frames, ticks = (sum(p[i] for p in perf) for i in range(3))
        lines = sum(p[3] for p in perf)
        fps = 50 * draws / frames
        print(f"  riding: {fps:.1f} fps, logic {50 * ticks / frames:.1f} Hz, "
              f"{lines / draws:.0f} raster lines of work a frame (of 312), worst {max(p[4] for p in perf)}")
        check("logic at 50 Hz", ticks >= frames * 0.98, f"{ticks}/{frames}")
        check("50 fps while riding", fps >= 49, f"{fps:.1f}")
    else:
        check("perf lines logged", False)
    ay = re.findall(r"ay mix0=(\d+) vol0=(\d+) mix1=(\d+)", log)
    check("the bass plays on chip 0 (tone A, volume 11)", any(int(a) & 1 == 0 and int(v) == 11 for a, v, _ in ay), str(ay[:3]))

    # game over -> fire -> initials (the default table: 1000 .. 200)
    e.press("A")
    more = ""
    for _ in range(60):
        e.run(5)
        more += e.log()
        if "hiscore rank" in more or "lunar: title" in more:
            break
    if final > 200:
        check("FIRE: high-score entry", "hiscore rank" in more, more[-200:])
        e.run(4)
        e.screenshot(SHOTS / "hiscore.png")
        e.type("BOT")
        e.run(20)
        more += e.log()
        check("initials entered", "lunar: name=BOT" in more, more[-200:])
        e.run_until("lunar: title", 200)
    check("back to the title", "lunar: title" in more + e.log(0)[-400:])
    e.run(30)
    shot(e, "hiscores")
    rec = e.record_input("save", REC)
    print(f"  recorded {rec['frames']} frames, {rec['events']} input changes")

# the same game, replayed twice from the recording: identical every frame
seqs = []
for i in range(2):
    with Emulator(machine="next") as e:
        r = e.replay(REC)
        seqs.append([h for _, h in r["hashes"]])
        check(f"replay {i + 1}: every frame hash matches the recorded game", r["ok"],
              f"{r.get('mismatches')} mismatches, first at {r.get('first_mismatch')}")
check("two replays give identical screens", seqs[0] == seqs[1] and len(seqs[0]) > 500)

print(f"{'FAIL' if failures else 'PASS'}: lunar_rider")
sys.exit(1 if failures else 0)
