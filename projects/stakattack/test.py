"""StakAttack (Next) - plays a whole game through the harness.

A bot reads the game state (`gs`, by symbol) and places each piece where a
classic stacking heuristic likes it best (few holes, low and flat stack,
complete rows), steering with the keys: rotate, move, slam. After a dozen
rows it stops thinking and slams every piece into the middle until the
stack tops out (the original has no lives: topping out ends the game).
Then game over, high-score entry, the title. Pause is tried on the way.
The game is recorded and replayed twice (every frame hash must match).
Screenshots go to screenshots/.
"""
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "zxdev" / "src"))

from zxdev.emu import Emulator  # noqa: E402

HERE = Path(__file__).resolve().parent
NEX = HERE / "build" / "stakattack.nex"
SHOTS = HERE / "screenshots"
REC = HERE / "build" / "bot-game.json"
W, H = 12, 19
failures = []
DEBUG = "--debug" in sys.argv

# piece_shapes[] in game.c: rows of 4 columns per rotation
SHAPES = [
    ["....|XXXX|....|....", "..X.|..X.|..X.|..X.", "....|....|XXXX|....", ".X..|.X..|.X..|.X.."],
    [".XX.|.XX.|....|...."] * 4,
    [".X..|XXX.|....|....", ".X..|.XX.|.X..|....", "....|XXX.|.X..|....", ".X..|XX..|.X..|...."],
    [".XX.|XX..|....|....", ".X..|.XX.|..X.|....", "....|.XX.|XX..|....", "X...|XX..|.X..|...."],
    ["XX..|.XX.|....|....", "..X.|.XX.|.X..|....", "....|XX..|.XX.|....", ".X..|XX..|X...|...."],
    ["..X.|XXX.|....|....", ".X..|.X..|.XX.|....", "....|XXX.|X...|....", "XX..|.X..|.X..|...."],
    ["X...|XXX.|....|....", ".XX.|.X..|.X..|....", "....|XXX.|..X.|....", ".X..|.X..|XX..|...."],
]
CELLS = [[[(r, c) for r, row in enumerate(s.split("|")) for c, ch in enumerate(row) if ch == "X"]
          for s in rots] for rots in SHAPES]


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


def read_gs(e):
    b = e.read_mem("gs", 277)
    field = [list(b[r * W:(r + 1) * W]) for r in range(H)]
    ptype, rot, x, y, nxt, state = struct.unpack("<6h", b[228:240])
    score, = struct.unpack("<I", b[240:244])
    level, lines = struct.unpack("<2h", b[244:248])
    clear_timer, = struct.unpack("<h", b[254:256])
    return dict(field=field, type=ptype, rot=rot, x=x, y=y, next=nxt, state=state,
                score=score, level=level, lines=lines, clear_timer=clear_timer)


def fits(field, cells, x, y):
    for r, c in cells:
        fx, fy = x + c, y + r
        if fx < 0 or fx >= W or fy < 0 or fy >= H or field[fy][fx]:
            return False
    return True


def evaluate(field, cells, x, y):
    f = [row[:] for row in field]
    for r, c in cells:
        f[y + r][x + c] = 1
    full = [i for i in range(H) if all(f[i])]
    f = [[0] * W for _ in full] + [row for i, row in enumerate(f) if i not in full]
    heights = []
    holes = 0
    for c in range(W):
        top = next((r for r in range(H) if f[r][c]), H)
        heights.append(H - top)
        holes += sum(1 for r in range(top, H) if not f[r][c])
    bump = sum(abs(a - b) for a, b in zip(heights, heights[1:]))
    return 0.76 * len(full) - 0.51 * sum(heights) - 0.36 * holes * 2 - 0.18 * bump - 0.3 * max(heights)


def plan(g):
    """(rotation, x) with the best landing spot reachable straight down."""
    best = None
    for rot in range(4):
        cells = CELLS[g["type"]][rot]
        for x in range(-2, W):
            if not fits(g["field"], cells, x, g["y"]):
                continue
            y = g["y"]
            while fits(g["field"], cells, x, y + 1):
                y += 1
            s = evaluate(g["field"], cells, x, y)
            if best is None or s > best[0]:
                best = (s, rot, x)
    return best[1:] if best else (g["rot"], g["x"])


def tap(e, key, until=None, frames=8):
    """Press a key for a frame, then run until `until(gs)` holds (the game has
    taken it) or `frames` pass; one release frame at least, so the next tap is
    a new press (the original's auto-repeat treats a held key differently)."""
    e.press(key, hold_frames=1, release_frames=1)
    n = 2
    while until and n < frames and not until(read_gs(e)):
        e.run(1)
        n += 1
    return n


def bot_game(e, smart_lines=12, max_frames=30000):
    log = ""
    frames = 0
    pieces = 0
    shots = set()
    paused = False
    while frames < max_frames:
        g = read_gs(e)
        if g["state"] == 3:
            break
        if g["state"] != 1 or g["clear_timer"] > 0:
            if g["clear_timer"] == 10 and "clear" not in shots:
                e.screenshot(SHOTS / "lineclear.png")
                shots.add("clear")
            e.run(1)
            frames += 1
            log += e.log()
            continue
        if pieces == 6 and not paused:                     # pause and resume once
            tap(e, "ENTER")
            e.run(20)
            e.screenshot(SHOTS / "paused.png")
            tap(e, "ENTER")
            frames += 24
            paused = True
        if g["lines"] < smart_lines:
            rot, x = plan(g)
            for _ in range(4):                             # rotate (wall kicks may move x)
                cur = read_gs(e)
                if cur["rot"] == rot:
                    break
                frames += tap(e, "Q", lambda a, r=cur["rot"]: a["rot"] != r)
            for _ in range(12):
                cur = read_gs(e)["x"]
                if cur == x:
                    break
                frames += tap(e, "O" if x < cur else "P", lambda a, c=cur: a["x"] != c)
                if read_gs(e)["x"] == cur:                 # blocked
                    if DEBUG:
                        a = read_gs(e)
                        print("  blocked at", cur, "->", x, "rot", a["rot"], "y", a["y"], "type", a["type"], "state", a["state"], "ct", a["clear_timer"])
                    break
        if DEBUG and g["lines"] < smart_lines:
            a = read_gs(e)
            print("type", g["type"], "plan", (rot, x), "got", (a["rot"], a["x"]))
        if pieces == 25 and "play" not in shots:
            e.screenshot(SHOTS / "play.png")
            shots.add("play")
        before = pieces
        frames += tap(e, "SPACE", lambda a: a["y"] < g["y"] or a["type"] != g["type"] or a["state"] != 1
                      or a["clear_timer"] > 0, 12)      # slam: a new piece (or a line clear)
        pieces += 1
        log += e.log()
    log += e.log()
    return log, pieces


with Emulator(machine="next") as e:
    e.load(NEX)
    check("boots to the title", e.run_until("stak: ready", 3000))
    check("TurboSound AY found", e.read_mem("ay_present", 1)[0] == 1)
    e.run(80)
    e.screenshot(SHOTS / "title.png")
    e.record_input("start", from_reset=True)
    e.run_until("stak: ready", 3000)
    e.run(80)
    tap(e, "SPACE")
    check("FIRE starts the game", e.run_until("stak: state=1", 100))
    e.run(40)
    log, pieces = bot_game(e)
    locks = [int(s) for s in re.findall(r"stak: lock score=(\d+)", log)]
    lines = [int(s) for s in re.findall(r"stak: lines=(\d+)", log)]
    check("pieces lock and score points", len(locks) > 20 and max(locks) > 500, f"{len(locks)} locks, {locks[-3:]}")
    check("rows are cleared", lines and max(lines) >= 10, str(lines[-3:]))
    check("ten rows: stage 2", "level=2" in log)
    check("pause and resume", "stak: state=2" in log and log.index("stak: state=2") < log.rindex("stak: state=1"))
    check("the stack tops out: game over", "stak: gameover" in log or e.run_until("stak: gameover", 500))
    m = re.search(r"gameover score=(\d+) lines=(\d+)", log + e.log(0))
    final = int(m.group(1)) if m else 0
    e.run(40)
    e.screenshot(SHOTS / "gameover.png")
    tap(e, "SPACE")
    more = ""
    for _ in range(100):
        e.run(5)
        more += e.log()
        if "hiscore rank" in more or "stak: title" in more:
            break
    if "hiscore rank" in more:
        e.run(40)                                          # the entry screen is drawn
        e.screenshot(SHOTS / "hiscore.png")
        e.type("BOT")
        e.run(10)
        more += e.log()
        check("high-score name entry", "stak: name=BOT" in more, more[-200:])
        e.run_until("stak: title", 300)
        more += "stak: title"
    else:
        check(f"score {final} below the table: no entry", final <= 1000)
    check("back to the title", "stak: title" in more)
    e.run(60)
    e.screenshot(SHOTS / "hiscores.png")
    rec = e.record_input("save", REC)
    print(f"  {pieces} pieces, final score {final}; recorded {rec['frames']} frames, {rec['events']} input changes")

seqs = []
for i in range(2):
    with Emulator(machine="next") as e:
        r = e.replay(REC)
        seqs.append([h for _, h in r["hashes"]])
        check(f"replay {i + 1}: every frame hash matches the recorded game", r["ok"],
              f"{r.get('mismatches')} mismatches, first at {r.get('first_mismatch')}")
check("two replays give identical screens", seqs[0] == seqs[1] and len(seqs[0]) > 500)

print(f"{'FAIL' if failures else 'PASS'}: stakattack")
sys.exit(1 if failures else 0)
