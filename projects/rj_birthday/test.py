"""RJ's Birthday Bash (Next) - a bot plays a whole game through the harness,
recorded from reset.

The title, the help (H) and the guest list (E) screens; Kempston fire
starts the party. The bot reads the game's state from memory (GameState at
_gs, its offsets as zsdcc lays it out: no padding) and walks: through the
foyer (its gifts score) to the Amsterdam lounge, over the plants that
appear there (100 each; they bring the pot police), then into the police:
busted, a life lost, three seconds in jail and back to the foyer; again
until the third arrest ends the party (game over). Fire, the name BOT and
ENTER put it in the high-score table; fire, BREAK past the guest list,
the credits, fire: the title.

Also: the scrolling (the camera across the house), the music on two
TurboSound chips, the frame rate in play, and the replay of the whole
session twice (every frame hash must match). Screenshots go to
screenshots/.
"""
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / "tools" / "zxdev" / "src"))

from zxdev.emu import Emulator  # noqa: E402
sys.path.insert(0, str(HERE / "tools"))
from bot import (ROOM_AMSTERDAM, ST_JAIL, ST_PLAYING, gs_addr, read_gs, walk)  # noqa: E402

NEX = HERE / "build" / "rj_birthday.nex"
SHOTS = HERE / "screenshots"
REC = HERE / "build" / "bot-game.json"
failures = []

def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


def until(e, text, frames):
    """run until a log line starts, then a frame more (the whole line)"""
    ok = e.run_until(text, frames)
    e.run(1)
    return ok


def perf(log):
    return [tuple(int(v) for v in m) for m in
            re.findall(r"perf loops=(\d+) frames=(\d+) copied=\d+ ay=(\d+),(\d+),(\d+),(\d+)", log)]


SHOTS.mkdir(exist_ok=True)
GS = gs_addr(HERE / "build" / "rj_birthday.map")
with Emulator(machine="next") as e:
    e.load(NEX)
    e.record_input("start", from_reset=True)
    check("boots to the title", until(e, "rj: state title", 400))
    check("Kempston found", "kempston=1" in e.log(0))
    e.run(40)
    e.screenshot(SHOTS / "title.png")
    e.press("H")
    check("H: how to party", until(e, "rj: state help", 20))
    e.run(5)
    e.screenshot(SHOTS / "help.png")
    e.press("SPACE")
    check("fire: back to the title", until(e, "rj: state title", 20))
    e.press("E")
    check("E: the guest list", until(e, "rj: state guest_edit", 20))
    e.press("A")                          # down: the cursor on the second guest
    e.run(5)
    e.screenshot(SHOTS / "guests.png")
    e.press("CAPS+SPACE")
    check("BREAK: back to the title", until(e, "rj: state title", 20))
    e.run(10)
    e.press("FIRE")
    check("Kempston fire starts the party", until(e, "rj: state playing score=0 lives=3", 30))
    play_from = len(e.log(0))
    e.run(60)
    e.screenshot(SHOTS / "foyer.png")
    g = read_gs(e, GS)
    # through the foyer to Amsterdam
    for _ in range(200):
        g = read_gs(e, GS)
        if g["room"] == ROOM_AMSTERDAM and g["x"] > 420:
            break
        walk(e, g, 480, 200)
    check("walked into the Amsterdam lounge (the house scrolls)", g["room"] == ROOM_AMSTERDAM and g["camera"] > 200,
          f"room {g['room']} camera {g['camera']}")
    shot_cops = shot_jail = False
    scores, lives_seen, plants_taken = [], [3], 0
    for _ in range(3000):
        g = read_gs(e, GS)
        if g["state"] == ST_JAIL:
            if not shot_jail:
                e.run(20)
                e.screenshot(SHOTS / "jail.png")
                shot_jail = True
            lives_seen.append(read_gs(e, GS)["lives"])
            until(e, "rj: state playing", 200)
            continue
        if g["state"] != ST_PLAYING:
            break
        scores.append(g["score"])
        if g["cops"] and not shot_cops and g["room"] == ROOM_AMSTERDAM:
            e.run(2)
            e.screenshot(SHOTS / "police.png")
            shot_cops = True
        if g["room"] != ROOM_AMSTERDAM or g["x"] < 340:
            walk(e, g, 480, 200)
        elif g["cops"] and (plants_taken >= 3 or len(lives_seen) > 1):
            cx, cy = min(g["cops"], key=lambda c: abs(c[0] - g["x"]) + abs(c[1] - g["y"]))
            walk(e, g, cx, cy, 2)
        elif g["plants"]:
            px, py = min(g["plants"], key=lambda p: abs(p[0] - g["x"]) + abs(p[1] - g["y"]))
            before = len(g["plants"])
            walk(e, g, px, py)
            if len(read_gs(e, GS)["plants"]) < before:
                plants_taken += 1
        else:
            walk(e, g, 480, 200)
    log = e.log(0)
    check("scores: gifts in the foyer, plants in Amsterdam", max(scores or [0]) >= 600, str(max(scores or [0])))
    check("plants picked up", plants_taken >= 3, str(plants_taken))
    check("the pot police came", shot_cops)
    check("busted: a life lost, the jail", "rj: state jail" in log and 2 in lives_seen, str(lives_seen))
    check("three arrests: game over", "rj: state gameover" in log, log[-300:])
    m = re.search(r"rj: state gameover score=(\d+)", log)
    final = int(m.group(1)) if m else 0
    pp = perf(log[play_from:])
    e.run(40)
    e.screenshot(SHOTS / "gameover.png")
    e.press("SPACE")
    check("fire: a new high score, the name", until(e, "rj: state enter_name", 30))
    for k in "BOT":
        e.press(k)
    e.run(16)
    e.screenshot(SHOTS / "name.png")
    e.press("ENTER")
    check("ENTER: the hall of party", until(e, "rj: state hiscore", 20))
    g = read_gs(e, GS)
    check("BOT in the table with the game's score", ("BOT", final) in g["hi"], str(g["hi"]))
    e.run(40)
    e.screenshot(SHOTS / "hiscores.png")
    e.press("SPACE")
    check("fire: join the guest list", until(e, "rj: state add_guest", 20))
    e.press("CAPS+SPACE")
    check("BREAK: the credits", until(e, "rj: state credits", 20))
    e.run(180)
    e.screenshot(SHOTS / "credits.png")
    e.press("SPACE")
    check("fire: back to the title", until(e, "rj: state title", 30))
    rec = e.record_input("save", REC)
    print(f"  recorded {rec['frames']} frames, {rec['events']} input changes")
    loops = sum(x[0] for x in pp)
    frames = sum(x[1] for x in pp)
    fps = loops * 50 / frames if frames else 0
    print(f"  playing: {fps:.1f} frames a second, final score {final}")
    check("at least 45 frames a second in play", fps >= 45, str(fps))
    check("the music on two TurboSound chips", any(x[2] or x[3] for x in pp) and any(x[4] or x[5] for x in pp),
          str(pp[:4]))
seqs = []
for i in range(2):
    with Emulator(machine="next") as e:
        r = e.replay(REC)
        seqs.append([h for _, h in r["hashes"]])
        check(f"replay {i + 1}: every frame hash matches the recorded game", r["ok"],
              f"{r.get('mismatches')} mismatches, first at {r.get('first_mismatch')}")
check("two replays give identical screens", seqs[0] == seqs[1] and len(seqs[0]) > 1000)
print(f"{'FAIL' if failures else 'PASS'}: rj_birthday")
sys.exit(1 if failures else 0)
