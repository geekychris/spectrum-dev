"""Frank the Frog (48K) - plays a whole game through the harness.

A bot reads the lanes (the game's `lanes` array, by symbol) and hops when the
next lane is safe: across the road, onto logs and rafts, into a free pond.
After a while it stops being careful, so the game ends: lives are lost, game
over, high-score entry, back to the title. The game is recorded and replayed
twice (every frame hash must match). Screenshots go to screenshots/.
"""
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "zxdev" / "src"))

from zxdev.emu import Emulator  # noqa: E402

HERE = Path(__file__).resolve().parent
TAP = HERE / "build" / "frank_the_frog.tap"
SHOTS = HERE / "screenshots"
REC = HERE / "build" / "bot-game.json"
LANE_SIZE, NUM_LANES = 39, 10        # lanes.h (packed)
HOMES = [32, 96, 176, 224]           # pond centres (lanes.c)
failures = []
DEBUG = "--debug" in sys.argv


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


def read_world(e):
    b = e.read_mem("lanes", LANE_SIZE * NUM_LANES)
    lanes = {}
    for i in range(NUM_LANES):
        L = b[i * LANE_SIZE:(i + 1) * LANE_SIZE]
        row, d, sp16, _, _, n = struct.unpack("<6h", L[:12])
        river = struct.unpack("<h", L[13:15])[0]
        objs = [struct.unpack("<2h", L[15 + 4 * j:19 + 4 * j]) for j in range(n)]
        lanes[row] = (d, sp16, river, objs)
    fx, frow, state = struct.unpack("<3h", e.read_mem("frank_probe", 6))
    filled = struct.unpack("<4h", e.read_mem("home_filled", 8))
    return fx, frow, state, lanes, filled


def at(obj, d, sp16, t):
    """Object position after t ticks, wrapping like lanes_tick()."""
    x16, w = obj
    for _ in range(t):
        x16 += sp16 if d > 0 else -sp16
        if d > 0 and x16 > (256 + 16) * 16:
            x16 = -w * 16
        elif d < 0 and x16 + w * 16 < -16 * 16:
            x16 = 256 * 16
    return x16 >> 4, w


def road_safe(px, lane, frames):
    d, sp16, _, objs = lane
    for t in range(frames):
        for o in objs:
            ox, ow = at(o, d, sp16, t)
            if px + 16 - 5 > ox + 2 - 3 and px + 5 < ox + ow - 2 + 3:   # the game's test + margin
                return False
    return True


def raft_under(px, lane, t):
    d, sp16, _, objs = lane
    for o in objs:
        ox, ow = at(o, d, sp16, t)
        if ox >= 0 and ox + ow <= 256 and px + 16 - 6 > ox and px + 6 < ox + ow:
            return True
    return False


def bot_game(e, careful_frames=2500):
    log = ""
    t = 0
    shots = set()
    history = []
    while "state=gameover" not in log and t < 20000:
        fx, frow, state, lanes, filled = read_world(e)
        hop = None
        careless = t > careful_frames
        if state == 1:                                   # playing
            nxt = frow - 1
            sx = (fx // 16) * 16          # a hop snaps Frank to his column (frank_move)
            ride = lambda row: ((1 if lanes[row][0] > 0 else -1) * lanes[row][1]) / 16
            if careless:
                hop = "Q"
            elif frow in lanes and lanes[frow][2]:       # riding in the river
                r = ride(frow)
                stays = all(raft_under(int(fx + r * k), lanes[frow], k) for k in range(1, 7))
                if nxt == 0:
                    free = [cx for cx, f in zip(HOMES, filled) if not f]
                    near = min(free, key=lambda cx: abs(sx + 8 - cx)) if free else None
                    if near is not None and abs(sx + 8 - near) <= 12:
                        hop = "Q"
                    elif near is not None and not stays:
                        hop = "A" if frow + 1 not in lanes or raft_under(sx, lanes[frow + 1], 2) else "Q"
                    elif near is not None:
                        step = "O" if near < sx + 8 else "P"
                        nx = sx - 16 if step == "O" else sx + 16
                        if all(raft_under(int(nx + r * k), lanes[frow], k) for k in range(1, 5)):
                            hop = step
                elif nxt in lanes and raft_under(sx, lanes[nxt], 2) and \
                        raft_under(int(sx + ride(nxt) * 8), lanes[nxt], 8):
                    hop = "Q"
                elif not stays:
                    down = frow + 1
                    if down not in lanes or raft_under(sx, lanes[down], 2):
                        hop = "A"
            elif nxt == 0:
                hop = None
            elif nxt in lanes and lanes[nxt][2]:         # from the median onto a log/raft
                if raft_under(sx, lanes[nxt], 2) and raft_under(int(sx + ride(nxt) * 8), lanes[nxt], 8):
                    hop = "Q"
            elif nxt in lanes:                           # road
                if road_safe(sx, lanes[nxt], 14):
                    hop = "Q"
            else:
                hop = "Q"                                # median / start
            # standing in a road lane that is about to get dangerous: up, else back, if safe
            if hop is None and frow in lanes and not lanes[frow][2] and not road_safe(fx, lanes[frow], 8):
                back = frow + 1
                if nxt in lanes and not lanes[nxt][2] and road_safe(sx, lanes[nxt], 8):
                    hop = "Q"
                elif nxt == 6:
                    hop = "Q"                            # the median is safe
                elif back not in lanes or road_safe(sx, lanes[back], 10):
                    hop = "A"
        history.append((t, frow, fx, hop, [at(o, lanes[frow][0], lanes[frow][1], 0) for o in lanes[frow][3]] if frow in lanes else None))
        if hop:
            e.press(hop, hold_frames=1, release_frames=0)
            t += 1
            for _ in range(8):                       # the hop lands at the next game loop
                nx, nrow = struct.unpack("<2h", e.read_mem("frank_probe", 4))
                if (nx // 16, nrow) != (fx // 16, frow) or nrow != frow:
                    break
                e.run(1)
                t += 1
        else:
            e.run(1)
            t += 1
        new = e.log()
        log += new
        if DEBUG and "death splat" in new:
            print("DEATH", new.strip().splitlines()[0])
            for h in history[-6:]:
                print("   ", h)
        if "home slot" in log and "home" not in shots:
            e.run(2)
            e.screenshot(SHOTS / "home.png")
            shots.add("home")
        if t > 300 and "play" not in shots and frow <= 5:
            e.screenshot(SHOTS / "play.png")
            shots.add("play")
    return log


with Emulator() as e:
    e.load(TAP)
    check("boots to the title", e.run_until("frank: ready", 3000))
    e.run(60)
    e.screenshot(SHOTS / "title.png")
    e.record_input("start", from_reset=True)
    e.run_until("frank: ready", 3000)
    e.run(30)
    e.press("SPACE")
    check("FIRE starts the game", e.run_until("frank: state=playing", 100))
    log = bot_game(e)
    scores = [int(s) for s in re.findall(r"frank: score=(\d+)", log)]
    lives = [int(s) for s in re.findall(r"frank: lives=(\d+)", log)]
    check("hops score points", scores and max(scores) >= 50, str(scores[-5:]))
    check("Frank reaches a pond", "home slot=" in log,
          "; ".join(l for l in log.splitlines() if "death" in l or "row=1 " in l or "row=0 " in l))
    check("a life is lost", lives and min(lives) < 3, str(lives))
    deaths = re.findall(r"death (\w+)", log)
    check("deaths by traffic or water", deaths and set(deaths) <= {"splat", "splash", "carried", "bank"}, str(deaths))
    check("game over", "state=gameover" in log)
    m = re.search(r"gameover score=(\d+)", log)
    final = int(m.group(1)) if m else 0
    e.run(40)
    e.screenshot(SHOTS / "gameover.png")
    e.press("SPACE")
    more = ""
    for _ in range(100):
        e.run(5)
        more += e.log()
        if "hiscore rank" in more or "frank: title" in more:
            break
    if "hiscore rank" in more:
        e.type("BOT")
        e.run(10)
        more += e.log()
        check("high-score name entry", "frank: name=BOT" in more, more[-200:])
        e.run_until("frank: title", 300)
        more += "frank: title"
    else:
        check(f"score {final} below the table: no entry", final <= 200)
    check("back to the title", "frank: title" in more)
    e.run(20)
    e.screenshot(SHOTS / "hiscores.png")
    rec = e.record_input("save", REC)
    print(f"  recorded {rec['frames']} frames, {rec['events']} input changes, final score {final}")

seqs = []
for i in range(2):
    with Emulator() as e:
        r = e.replay(REC)
        seqs.append([h for _, h in r["hashes"]])
        check(f"replay {i + 1}: every frame hash matches the recorded game", r["ok"],
              f"{r.get('mismatches')} mismatches, first at {r.get('first_mismatch')}")
check("two replays give identical screens", seqs[0] == seqs[1] and len(seqs[0]) > 500)

print(f"{'FAIL' if failures else 'PASS'}: frank_the_frog")
sys.exit(1 if failures else 0)
