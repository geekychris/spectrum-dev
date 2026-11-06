"""Spectral Keep (Next) - plays a game through the harness.

The bot (tools/bot.py, the same one that plays the host sim) reads the game
through `keep_probe` (src/probe.c) every frame and answers with the keys:
it starts The Gatehouse, walks out of the gate room's north door into the
Great Hall, round its west side to the relic in the far corner, then stands
still while the hall's ghost takes all four lives: game over, the title.
The game is recorded and replayed twice (every frame hash must match).
Screenshots go to screenshots/.
"""
import re
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / "tools" / "zxdev" / "src"))
sys.path.insert(0, str(HERE / "tools"))

from zxdev.emu import Emulator  # noqa: E402
from bot import B_A, B_DOWN, B_LEFT, B_RIGHT, B_UP, PROBE_SIZE, Bot, decode  # noqa: E402

NEX = HERE / "build" / "spectral_keep.nex"
SHOTS = HERE / "screenshots"
REC = HERE / "build" / "bot-game.json"
KEYS = [(B_UP, "Q"), (B_DOWN, "A"), (B_LEFT, "O"), (B_RIGHT, "P"), (B_A, "SPACE")]
failures = []


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


def play(e, frames=4000):
    """the bot until it is back on the title; returns the log"""
    bot = Bot()
    log = ""
    shots = {}
    down = ""
    for t in range(frames):
        p = decode(e.read_mem("keep_probe", PROBE_SIZE))
        held, _ = bot.step(p)
        keys = " ".join(k for bit, k in KEYS if held & bit)
        if keys != down:                     # hold() adds to what is held: release first
            e.release()
            if keys:
                e.hold(keys)
            down = keys
        e.run(1)
        log += e.log()
        if "hall" not in shots and "room drawn hall" in log:
            shots["hall"] = t
        if "play" not in shots and "hall" in shots and t > shots["hall"] + 60:
            e.screenshot(SHOTS / "play.png")
            shots["play"] = t
        if "relic" not in shots and "pickup R" in log:
            e.run(2)
            e.screenshot(SHOTS / "relic.png")
            shots["relic"] = t
        if "over" not in shots and "state=4" in log:
            e.run(20)
            e.screenshot(SHOTS / "gameover.png")
            shots["over"] = t
        if bot.phase == "done":
            break
    e.release()
    return log, bot


SHOTS.mkdir(exist_ok=True)
with Emulator(machine="next") as e:
    e.load(NEX)
    check("boots to the title", e.run_until("keep: room drawn gate", 600))
    e.run(60)
    e.screenshot(SHOTS / "title.png")
    e.record_input("start", from_reset=True)
    log, bot = play(e)
    full = e.log(0)
    check("FIRE starts The Gatehouse", "keep: level THE GATEHOUSE" in full)
    check("through the north door into the Great Hall", "room hall visited=2/6" in full)
    check("the relic picked up", "pickup R hall:56 relics=1" in full, full[-300:])
    deaths = re.findall(r"death A GHOST room=hall lives=(\d)", full)
    check("the ghost takes four lives", deaths == ["3", "2", "1", "0"], str(deaths))
    check("game over", "state=4" in full)
    check("back to the title", bot.phase == "done", bot.phase)
    over = struct.unpack("<H", e.read_mem("frame_overruns", 2))[0]
    frames = struct.unpack("<H", e.read_mem("frame_count", 2))[0]
    print(f"  ({frames} frames, {over} overruns)")
    check("no sprite patterns dropped", "overflow=0" in full and not re.search(r"overflow=[1-9]", full))
    rec = e.record_input("save", REC)
    print(f"  recorded {rec['frames']} frames, {rec['events']} input changes")

seqs = []
for i in range(2):
    with Emulator(machine="next") as e:
        r = e.replay(REC)
        seqs.append([h for _, h in r["hashes"]])
        check(f"replay {i + 1}: every frame hash matches the recorded game", r["ok"],
              f"{r.get('mismatches')} mismatches, first at {r.get('first_mismatch')}")
check("two replays give identical screens", seqs[0] == seqs[1] and len(seqs[0]) > 1000)

print(f"{'FAIL' if failures else 'PASS'}: spectral_keep")
sys.exit(1 if failures else 0)
