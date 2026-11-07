"""Dot Chase (48K) - plays a whole game through the harness.

A bot reads the game state from memory (at 0x7B00; `dc_layout` gives the
offsets: maze first, the ladybug, the spiders, the state) and steers the ladybug with the keys: it
walks to the nearest seed along paths that stay clear of the spiders, chases
scared spiders, and runs from hunting ones. Once it has cleared the first
garden it stops looking out for spiders, so the game ends: lives are lost,
game over, high-score entry, back to the title. The game is recorded and
replayed twice (every frame hash must match). Screenshots go to screenshots/.
"""
import re
import struct
import sys
from collections import deque
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "zxdev" / "src"))

from zxdev.emu import Emulator  # noqa: E402

HERE = Path(__file__).resolve().parent
TAP = HERE / "build" / "dot_chase.tap"
SHOTS = HERE / "screenshots"
REC = HERE / "build" / "bot-game.json"
GAME = 0x7B00                       # the game state (fixed.asm); maze[31][28] first
W, H, TUNNEL = 28, 31, 14
WALL, DOOR = 1, 4
GAME_SIZE = 1080
FRIGHT, EATEN = 2, 3                # spider states
PLAYING = 2
KEYS = {0: "P", 1: "A", 2: "O", 3: "Q"}   # right, down, left, up
DX, DY = (1, 0, -1, 0), (0, 1, 0, -1)
failures = []
DEBUG = "--debug" in sys.argv


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


def tile(p):
    """the game's tile of a world position (centre / 8, toward zero)"""
    return int((p + 4) / 8)


def open_tile(maze, x, y):
    if y == TUNNEL and (x < 0 or x >= W):
        return True
    if not (0 <= x < W and 0 <= y < H):
        return False
    return maze[y][x] not in (WALL, DOOR)


def step(x, y, d):
    nx, ny = x + DX[d], y + DY[d]
    if ny == TUNNEL:
        nx %= W
    return nx, ny


def next_centre(hx, hy, d):
    """the tile centre the ladybug reaches next (where it can turn)"""
    if hx % 8 == 0 and hy % 8 == 0:
        return hx // 8, hy // 8
    if d == 0:
        return hx // 8 + 1, hy // 8
    if d == 2:
        return hx // 8, hy // 8
    if d == 1:
        return hx // 8, hy // 8 + 1
    return hx // 8, hy // 8


def bfs(maze, start, blocked):
    """first step and distance to every reachable tile"""
    first = {start: (None, 0)}
    q = deque([start])
    while q:
        x, y = q.popleft()
        f, dist = first[(x, y)]
        for d in range(4):
            n = step(x, y, d)
            if n in first or n in blocked or not open_tile(maze, *n):
                continue
            first[n] = (d if f is None else f, dist + 1)
            q.append(n)
    return first


def choose(maze, p, careless):
    hx, hy, hdir, state, sx, sy, sst, shome, fright, _ = p
    cx, cy = next_centre(hx, hy, hdir)
    cx %= W
    hunters, prey = [], []
    for i in range(4):
        if shome[i] or sst[i] == EATEN:
            continue
        t = (tile(sx[i]) % W, tile(sy[i]))
        (prey if sst[i] == FRIGHT and fright > 60 else hunters).append(t)
    blocked = set()
    if not careless:
        for (tx, ty) in hunters:            # a spider and the tiles around it
            for dx in range(-2, 3):
                for dy in range(-2, 3):
                    if abs(dx) + abs(dy) <= 2:
                        blocked.add(((tx + dx) % W, ty + dy))
        blocked.discard((cx, cy))
    paths = bfs(maze, (cx, cy), blocked)
    goals = [t for t in prey if t in paths and 0 < paths[t][1] < 10]
    if not goals:     # seeds and flowers (not the one on the tile it is about to eat)
        goals = [(x, y) for (x, y) in paths if 0 <= x < W and maze[y][x] in (2, 3) and (x, y) != (cx, cy)]
    if goals:
        g = min(goals, key=lambda t: paths[t][1])
        if paths[g][0] is not None:
            return paths[g][0]
    # nowhere safe to go: move away from the nearest hunter
    best, far = hdir, -1
    for d in range(4):
        n = step(cx, cy, d)
        if open_tile(maze, *n):
            dmin = min((abs(n[0] - h[0]) + abs(n[1] - h[1]) for h in hunters), default=99)
            if dmin > far:
                best, far = d, dmin
    return best


LAYOUT = None


def read(e):
    """the maze and (hx, hy, hdir, state, sx, sy, sstate, shome, fright, lives)"""
    global LAYOUT
    if LAYOUT is None:
        LAYOUT = struct.unpack("<10H", e.read_mem("dc_layout", 20))
    hero, spiders, ssize, s_state, s_home, state, fr_on, fr_t, h_dir, lives = LAYOUT
    g = e.read_mem(GAME, GAME_SIZE)
    maze = [list(g[y * W:(y + 1) * W]) for y in range(H)]
    i16 = lambda o: struct.unpack_from("<h", g, o)[0]
    sp = [spiders + i * ssize for i in range(4)]
    p = (i16(hero + 4), i16(hero + 6), i16(hero + h_dir), i16(state),
         [i16(o + 4) for o in sp], [i16(o + 6) for o in sp],
         [i16(o + s_state) for o in sp], [i16(o + s_home) for o in sp],
         i16(fr_t) if i16(fr_on) else 0, i16(lives))
    return maze, p


def bot_game(e, max_frames=40000):
    log, t, held, shots = "", 0, None, set()
    careless = False
    while "dot: gameover" not in log and t < max_frames:
        maze, p = read(e)
        if p[3] == PLAYING or p[3] == 1:
            d = choose(maze, p, careless)
            if KEYS[d] != held:
                e.release()
                e.hold(KEYS[d])
                held = KEYS[d]
        e.run(2)
        t += 2
        new = e.log()
        log += new
        if "level clear" in new or t > 15000:
            careless = True
        if DEBUG and "death" in new:
            print("   ", new.strip().splitlines()[-1], p)
        if "play" not in shots and t > 900:
            e.screenshot(SHOTS / "play.png")
            shots.add("play")
        if "fright" not in shots and "dot: flower" in log and t > 0:
            e.run(20)
            e.screenshot(SHOTS / "scared.png")
            shots.add("fright")
            log += e.log()
    if held:
        e.release()
    return log, t


SHOTS.mkdir(exist_ok=True)
with Emulator() as e:
    e.load(TAP)
    check("boots to the title", e.run_until("dot: ready", 3000))
    e.run(60)
    e.screenshot(SHOTS / "title.png")
    e.run(400)
    e.screenshot(SHOTS / "hiscores.png")
    e.record_input("start", from_reset=True)
    e.run_until("dot: ready", 3000)
    e.run(30)
    e.press("SPACE")
    check("FIRE starts the game", e.run_until("dot: state=ready", 100))
    e.run(20)
    e.screenshot(SHOTS / "ready.png")
    log, frames = bot_game(e)
    scores = [int(s) for s in re.findall(r"score=(\d+)", log)]
    lives = [int(s) for s in re.findall(r"dot: lives=(\d+)", log)]
    check("seeds score points", scores and max(scores) >= 500, str(scores[-3:]))
    check("a flower scares the spiders", "dot: flower" in log)
    check("a scared spider is caught", "ate spider" in log)
    check("the first garden is cleared", "level clear level=1" in log and "dot: level=2" in log)
    check("lives are lost", lives and min(lives) < 3, str(lives))
    check("game over", "dot: gameover" in log)
    perf = [tuple(map(int, m)) for m in re.findall(r"perf frames=(\d+) drawn=(\d+) maxticks=(\d+)", log)]
    if perf:
        fr, dr = sum(p[0] for p in perf), sum(p[1] for p in perf)
        print(f"  {dr} pictures in {fr} frames: {50 * dr / fr:.1f} fps, worst frame {max(p[2] for p in perf)} ticks")
        print("  fps each 10 s:", " ".join(f"{50 * p[1] / p[0]:.0f}" for p in perf))
        check("the picture keeps up with the 50 Hz logic (>= 45 fps)", 50 * dr / fr >= 45, f"{50 * dr / fr:.1f}")
    m = re.search(r"gameover score=(\d+)", log)
    final = int(m.group(1)) if m else 0
    e.run(30)
    e.screenshot(SHOTS / "gameover.png")
    e.run(150)
    e.press("SPACE")
    more = ""
    for _ in range(100):
        e.run(5)
        more += e.log()
        if "hiscore rank" in more or "dot: title" in more:
            break
    if "hiscore rank" in more:
        e.type("BOT")
        e.run(10)
        more += e.log()
        check("high-score name entry", "dot: name=BOT" in more, more[-200:])
        e.run_until("dot: title", 300)
        more += "dot: title"
    else:
        check(f"score {final} below the table: no entry", final <= 2000)
    check("back to the title", "dot: title" in more)
    e.run(420)
    e.screenshot(SHOTS / "table.png")
    rec = e.record_input("save", REC)
    print(f"  recorded {rec['frames']} frames, {rec['events']} input changes, final score {final}")

seqs = []
for i in range(2):
    with Emulator() as e:
        r = e.replay(REC)
        seqs.append([h for _, h in r["hashes"]])
        check(f"replay {i + 1}: every frame hash matches the recorded game", r["ok"],
              f"{r.get('mismatches')} mismatches, first at {r.get('first_mismatch')}")
check("two replays give identical screens", seqs[0] == seqs[1] and len(seqs[0]) > 1000)

print(f"{'FAIL' if failures else 'PASS'}: dot_chase")
sys.exit(1 if failures else 0)
