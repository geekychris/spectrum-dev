"""Orb Hunter (Next) - plays through the harness.

The rules were checked against the original on the host (make -C tests).
Here: boots to the title (28 MHz, the title picture, the music on TurboSound
chip 1), the Kempston and the keys move the hunter, then, recorded from
reset, a game where tools/bot.py runs from the start room into the next
cavern and shoots a creature (score), then walks into the others until they
wear its energy down (hits, death); game over, initials typed into the
high-score table, back to the title. The recording is replayed twice: every
frame hash must match. Screenshots go to screenshots/, the frame rate is
reported.
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
from bot import DEAD, PLAYING, PROBE_SIZE, S_ITEM, S_NAME, S_OVER, S_TITLE, Bot, decode  # noqa: E402

NEX = HERE / "build" / "orb_hunter.nex"
SHOTS = HERE / "screenshots"
REC = HERE / "build" / "bot-game.json"
TITLE_BANK = 25
failures = []


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


def probe(e):
    return decode(e.read_mem("orb_probe", PROBE_SIZE))


def wait_for(e, cond, frames=60):
    for _ in range(frames):
        if cond(probe(e)):
            return True
        e.run(1)
    return cond(probe(e))


def play(e, bot, frames=6000):
    """the bot's keys every frame until the game is over; screenshots on the way"""
    down, shots = "", {}
    for _ in range(frames):
        p = probe(e)
        if p["state"] in (S_OVER, S_NAME):
            break
        keys = bot.keys(p)
        if keys != down:
            e.release()
            if keys:
                e.hold(keys)
            down = keys
        e.run(1)
        if "fight" not in shots and p["room_x"] == 3 and p["score"] == 0 and bot.t > 120:
            e.screenshot(SHOTS / "fight.png")
            shots["fight"] = 1
        if "hit" not in shots and p["state"] == PLAYING and p["health"] < 20 and p["health"] > 0:
            e.screenshot(SHOTS / "hit.png")
            shots["hit"] = 1
        if "dead" not in shots and p["state"] == DEAD:
            e.run(8)
            e.screenshot(SHOTS / "dead.png")
            shots["dead"] = 1
    e.release()


SHOTS.mkdir(exist_ok=True)
with Emulator(machine="next") as e:
    e.load(NEX)
    check("boots to the title", e.run_until("orb: ready", 300))
    e.run(2)
    check("28 MHz", "turbo=3" in e.log(0))
    check("the title picture is shown (its own Layer 2 banks)", e._reg(0x12) == TITLE_BANK)
    e.run(30)
    e.screenshot(SHOTS / "title.png")
    mark = len(e.log(0))
    e.run(210)
    m = re.findall(r"ay effects=(\d+) music=(\d+)", e.log(0)[mark:])
    check("the music plays on TurboSound chip 1", m and int(m[-1][1]) > 50, str(m))
    e.joystick("Kempston")
    e.press("FIRE")
    check("Kempston fire starts a game in the start room", e.run_until("start room=2,2", 20))
    check("the game's screen on Layer 2", wait_for(e, lambda p: p["state"] == PLAYING) and e._reg(0x12) == 16)
    x0 = probe(e)["hx"]
    e.hold("RIGHT")
    e.run(20)
    e.release()
    check("Kempston right runs right", probe(e)["hx"] > x0 + 20, f"{x0} -> {probe(e)['hx']}")
    e.hold("O")
    e.run(20)
    e.release()
    check("O runs left", probe(e)["hx"] < x0 + 20 and probe(e)["facing"] == 1)
    e.screenshot(SHOTS / "start_room.png")
    # right, jumping now and then: down the start room's gap, on to the Spring Boots in room 3,3
    mark = len(e.log(0))
    for t in range(900):
        if "item " in e.log(0)[mark:]:
            break
        e.release()
        e.hold("P Q" if t % 50 < 12 else "P")
        e.run(1)
    e.release()
    got = re.findall(r"item ([A-Z ]+)", e.log(0)[mark:])
    check("a power-up: SPRING BOOTS in room 3,3, its screen", got[:1] == ["SPRING BOOTS"] and wait_for(e, lambda q: q["state"] == S_ITEM), str(got))
    e.run(10)
    e.screenshot(SHOTS / "item.png")
    e.press("SPACE")
    check("fire carries on", wait_for(e, lambda q: q["state"] == PLAYING))

    # ---- the game, recorded from reset
    e.record_input("start", from_reset=True)
    e.run_until("orb: ready", 300)
    e.run(40)
    e.press("SPACE")
    check("SPACE starts a game", e.run_until("start room=2,2", 20))
    bot = Bot()
    play(e, bot)
    full = e.log(0)
    p = probe(e)
    check("out of the start room into the caverns", re.search(r"room [0-9],[0-9]", full) is not None)
    kills = re.findall(r"score (\d+)", full)
    check("shoots a creature: 100 points", kills[:1] == ["100"], str(kills))
    check("the creatures hit back (5 energy a hit)", len(re.findall(r"hit health=", full)) >= 6)
    check("the hunter dies", "dead score=" in full)
    check("game over", p["state"] == S_OVER, str(p))
    e.run(10)
    e.screenshot(SHOTS / "game_over.png")
    over = re.findall(r"gameover score=(\d+) rank=(-?\d+)", full)
    check("the score makes the table", over and int(over[-1][1]) >= 0, str(over))
    e.press("SPACE")
    check("FIRE: initials", e.run_until("hiscore rank=", 30) and wait_for(e, lambda q: q["state"] == S_NAME))
    e.run(5)
    e.screenshot(SHOTS / "initials.png")
    e.type("BOT")
    check("typed BOT", e.run_until("name=BOT", 60))
    check("back to the title", e.run_until("state title", 30) and wait_for(e, lambda q: q["state"] == S_TITLE))
    table = e.read_mem("hs_table", 5 * 8)
    entries = [(table[i * 8:i * 8 + 3].decode(), struct.unpack("<L", table[i * 8 + 4:i * 8 + 8])[0]) for i in range(5)]
    check("BOT is in the table", any(n == "BOT" and s == p["score"] for n, s in entries), str(entries))
    e.run(40)
    e.screenshot(SHOTS / "title_table.png")
    fps = [int(f) for f, s in re.findall(r"perf fps=(\d+) steps=\d+ frames=\d+ game=\d+ shell=(\d)", full) if s == "1"]
    steps = [(int(s), int(g)) for s, g in re.findall(r"perf fps=\d+ steps=(\d+) frames=\d+ game=(\d+)", full)]
    if fps:
        print(f"  frame rate while playing: {sum(fps) / len(fps):.1f} fps on average, {min(fps)} at the lowest")
    check("50 steps a second in play, the original's speed", steps and all(abs(s - g) <= 1 for s, g in steps),
          str(steps[:6]))
    check("40 fps or more while playing", fps and sum(fps[1:]) / max(1, len(fps) - 1) >= 40, str(fps))
    rec = e.record_input("save", REC)
    print(f"  recorded {rec['frames']} frames, {rec['events']} input changes; score {p['score']}")

seqs = []
for i in range(2):
    with Emulator(machine="next") as e:
        r = e.replay(REC)
        seqs.append([h for _, h in r["hashes"]])
        check(f"replay {i + 1}: every frame hash matches the recorded game", r["ok"],
              f"{r.get('mismatches')} mismatches, first at {r.get('first_mismatch')}")
check("two replays give identical screens", seqs[0] == seqs[1] and len(seqs[0]) > 500)

print(f"{'FAIL' if failures else 'PASS'}: orb_hunter")
sys.exit(1 if failures else 0)
