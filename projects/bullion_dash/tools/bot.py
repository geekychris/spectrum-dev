"""A Bullion Dash player for the tests: reads the game's state (`gs`) from the
emulator and chooses the keys, planning with the game's own rules built for
the host (tools/hostsim.c: the same player.c, enemy.c, level.c, play.c).

    bot = Bot()                     # builds build/hostsim.so if needed
    keys = bot.decide(gs_bytes, rng_bytes)   # -> e.g. "P", "SPACE P", ""

A decision is taken when the player stands on a cell: for each first move
(left, right, up, down, dig left/right and drop, wait) the planner runs the
real rules forward for a while, the player then heading for the nearest gold
(or the top row once the gold is in) along a path over the grid, and keeps
the move that gets the most gold and the closest without getting caught.
"""
import ctypes
import heapq
import struct
import subprocess
from pathlib import Path

HERE = Path(__file__).resolve().parents[1]
SDK = HERE.parents[1] / "sdk"
SO = HERE / "build" / "hostsim.so"

# game.h
COLS, ROWS, TW, TH = 28, 16, 10, 14
EMPTY, BRICK, SOLID, LADDER, BAR, GOLD, HIDDEN, PLAYER_M, ENEMY_M, TRAP = range(10)
PS_FALLING, PS_DIGGING, PS_DEAD = 4, 5, 6
ES_TRAPPED, ES_DEAD = 4, 5
IN_LEFT, IN_RIGHT, IN_UP, IN_DOWN, IN_FIRE = 1, 2, 4, 8, 16
PLAY_ON, PLAY_CAUGHT, PLAY_BURIED, PLAY_GAVE_UP, PLAY_WON = range(5)
OFF_TILES = 98
KEYS = {IN_LEFT: "O", IN_RIGHT: "P", IN_UP: "Q", IN_DOWN: "A", IN_FIRE: "SPACE"}

# move costs in frames (the original's speeds: 2 px a frame, 10 x 14 tiles)
COST_SIDE, COST_UPDOWN, COST_FALL, COST_DIG = 5, 7, 7, 12


def build_sim():
    srcs = [HERE / "src" / f for f in ("player.c", "enemy.c", "level.c", "play.c", "coords.c", "levels_data.c")]
    srcs += [SDK / "src" / "gridbfs.c", SDK / "src" / "gridpack.c", HERE / "tools" / "hostsim.c"]
    if SO.exists() and all(SO.stat().st_mtime > s.stat().st_mtime for s in srcs + [HERE / "src" / "game.h"]):
        return
    SO.parent.mkdir(exist_ok=True)
    subprocess.run(["cc", "-O2", "-shared", "-fPIC", "-I", str(HERE / "src"), "-I", str(SDK / "include"),
                    *map(str, srcs), "-o", str(SO)], check=True)


class State:
    """The fields of gs the bot uses (game.h's layout)."""

    def __init__(self, b):
        self.raw = b
        self.state = b[0]
        self.level = struct.unpack_from("<h", b, 1)[0]
        self.score = struct.unpack_from("<I", b, 3)[0]
        self.lives, self.gold_total, self.gold = struct.unpack_from("<3h", b, 7)
        self.frame = struct.unpack_from("<H", b, 13)[0]
        self.px, self.py, self.gx, self.gy, self.dir, self.pstate = struct.unpack_from("<hhbbBB", b, 15)
        self.enemies = []
        for i in range(b[96]):
            px, py, gx, gy, _, st, _, _, has_gold, active, _ = struct.unpack_from("<hhbbBBBhBBB", b, 26 + 14 * i)
            if active:
                self.enemies.append((px, py, gx, gy, st, has_gold))
        self.tiles = b[OFF_TILES:OFF_TILES + COLS * ROWS]

    def tile(self, x, y):
        if not (0 <= x < COLS and 0 <= y < ROWS):
            return SOLID
        return self.tiles[y * COLS + x]

    @property
    def aligned(self):
        return self.px % TW == 0 and self.py % TH == 0


def _enterable(t):
    return t in (EMPTY, GOLD, LADDER, BAR, HIDDEN, TRAP)


def _solid(t):
    return t in (BRICK, SOLID, TRAP)


class Grid:
    """The player's moves over the tiles (player.c's rules), falls resolved:
    edges (move, destination, cost) out of each cell."""

    def __init__(self, tile):
        self.tile = tile

    def support(self, x, y):
        if y >= ROWS - 1 or self.tile(x, y) == LADDER:
            return True
        b = self.tile(x, y + 1)
        return _solid(b) or b == LADDER

    def holds(self, x, y):
        return self.tile(x, y) in (LADDER, BAR) or self.support(x, y)

    def land(self, x, y, cost=0):
        """Where a player let go at (x, y) ends up: (x, y', cost) or None (off the grid)."""
        while not self.holds(x, y):
            y += 1
            cost += COST_FALL
            if y >= ROWS:
                return None
        return x, y, cost

    def edges(self, x, y):
        out = []
        t = self.tile(x, y)
        for d, move, dig in ((-1, IN_LEFT, "dl"), (1, IN_RIGHT, "dr")):
            nx = x + d
            if 0 <= nx < COLS and _enterable(self.tile(nx, y)):
                dest = self.land(nx, y, COST_SIDE)
                if dest:
                    out.append((move, dest[:2], dest[2]))
            # dig the brick below-left/right and drop through it (only when the
            # fall goes on below the hole: a player left in the hole is buried)
            if (0 <= nx < COLS and self.support(x, y) and self.tile(nx, y + 1) == BRICK
                    and _enterable(self.tile(nx, y)) and y + 2 < ROWS):
                under = Grid(lambda X, Y: EMPTY if (X, Y) == (nx, y + 1) else self.tile(X, Y))
                if not under.holds(nx, y + 1):
                    dest = under.land(nx, y + 1, COST_DIG + COST_SIDE + COST_FALL)
                    if dest:
                        out.append((dig, dest[:2], dest[2]))
        if t == LADDER and y > 0 and _enterable(self.tile(x, y - 1)):
            dest = self.land(x, y - 1, COST_UPDOWN)
            if dest:
                out.append((IN_UP, dest[:2], dest[2]))
        if y < ROWS - 1 and (t == LADDER or self.tile(x, y + 1) == LADDER) and _enterable(self.tile(x, y + 1)):
            dest = self.land(x, y + 1, COST_UPDOWN)
            if dest:
                out.append((IN_DOWN, dest[:2], dest[2]))
        return out

    def dist_to(self, goals):
        """Frames to the nearest goal from every cell (Dijkstra on reversed edges)."""
        back = {}
        for y in range(ROWS):
            for x in range(COLS):
                for _, d, c in self.edges(x, y):
                    back.setdefault(d, []).append(((x, y), c))
        dist = {g: 0 for g in goals}
        q = [(0, g) for g in goals]
        heapq.heapify(q)
        while q:
            dd, c = heapq.heappop(q)
            if dd > dist.get(c, 1 << 30):
                continue
            for p, cost in back.get(c, ()):
                if dd + cost < dist.get(p, 1 << 30):
                    dist[p] = dd + cost
                    heapq.heappush(q, (dd + cost, p))
        return dist


class Bot:
    def __init__(self, horizon=90):
        build_sim()
        self.lib = lib = ctypes.CDLL(str(SO))
        lib.sim_gs.restype = ctypes.c_void_p
        lib.sim_rng.restype = ctypes.c_void_p
        lib.sim_tick.restype = ctypes.c_uint8
        self.size = lib.sim_gs_size()
        self.horizon = horizon
        self.plan = None            # the move being made (decide)
        self.decisions = 0

    # --- the host simulation
    def _put(self, gs, rng):
        ctypes.memmove(self.lib.sim_gs(), gs, self.size)
        ctypes.memmove(self.lib.sim_rng(), rng, 4)
        self.lib.sim_sync()

    def _get(self):
        return State(ctypes.string_at(self.lib.sim_gs(), self.size))

    @staticmethod
    def goals(s):
        if s.gold < s.gold_total:
            g = [(x, y) for y in range(ROWS) for x in range(COLS) if s.tile(x, y) == GOLD]
            if g:
                return g
        return [(x, 0) for x in range(COLS) if _enterable(s.tile(x, 0)) or s.tile(x, 0) == HIDDEN]

    @staticmethod
    def keys_for(s, move):
        """The keys that carry out a move from where the player stands."""
        if move in ("dl", "dr"):
            d = -1 if move == "dl" else 1
            side = IN_LEFT if d < 0 else IN_RIGHT
            if s.tile(s.gx + d, s.gy + 1) == BRICK and s.pstate != PS_DIGGING:
                return IN_FIRE | side
            return side
        return move or 0

    def _policy(self, s, dist):
        """Greedy step along the distance field (for rollouts and execution)."""
        if s.pstate in (PS_FALLING, PS_DIGGING) or not s.aligned:
            return None
        best, keys = None, 0
        for move, dest, cost in Grid(s.tile).edges(s.gx, s.gy):
            v = cost + dist.get(dest, 1 << 20)
            if best is None or v < best:
                best, keys = v, self.keys_for(s, move)
        return keys

    def macros(self, s):
        """The moves open to a player standing on a cell: the grid's edges, a
        trap dug left or right (no drop), and waiting."""
        out = [m for m, _, _ in Grid(s.tile).edges(s.gx, s.gy)]
        for d, m in ((-1, "tl"), (1, "tr")):
            if s.tile(s.gx + d, s.gy + 1) == BRICK and _enterable(s.tile(s.gx + d, s.gy)):
                out.append(m)
        return list(dict.fromkeys(out)) + ["wait"]

    def _macro(self, m, limit=30):
        """Run move m in the simulation until the player stands on a cell
        again; returns the play result (PLAY_ON while going on) and frames."""
        s0 = self._get()
        for t in range(limit):
            s = self._get()
            if m == "wait":
                if t >= 6:
                    return PLAY_ON, t
                keys = 0
            elif m in ("tl", "tr"):
                if t and s.pstate != PS_DIGGING:
                    return PLAY_ON, t
                keys = (IN_FIRE | (IN_LEFT if m == "tl" else IN_RIGHT)) if s.pstate != PS_DIGGING else 0
            else:
                if t and (s.gx, s.gy) != (s0.gx, s0.gy) and s.aligned and s.pstate not in (PS_FALLING, PS_DIGGING):
                    return PLAY_ON, t
                keys = self.keys_for(s, m)
            r = self.lib.sim_tick(keys)
            if r != PLAY_ON:
                return r, t
        return PLAY_ON, limit

    def _finish(self, dist, gold0, t0):
        """Greedy along the field to the horizon; the score: gold sooner is
        better, then being closer to the next goal, then away from the guards."""
        keys, reward, gold = 0, 0.0, self._get().gold
        reward += (gold - gold0) * (1e5 - 50 * t0)
        for t in range(t0, self.horizon):
            s = self._get()
            if s.gold > gold:
                reward += (s.gold - gold) * (1e5 - 50 * t)
                gold = s.gold
            k = self._policy(s, dist)
            if k is not None:
                keys = k
            elif s.pstate in (PS_DIGGING, PS_FALLING):
                keys = 0
            r = self.lib.sim_tick(keys)
            if r == PLAY_WON:
                return 1e9 - t
            if r != PLAY_ON:
                return -1e9 + t * 1e6 + reward
        s = self._get()
        reward += (s.gold - gold) * (1e5 - 50 * self.horizon)
        here = dist.get((s.gx, s.gy), 5000)
        danger = min((abs(e[0] - s.px) + abs(e[1] - s.py) for e in s.enemies
                      if e[4] not in (ES_TRAPPED, ES_DEAD)), default=999)
        return reward - here * 10 + min(danger, 40)

    def _value(self, gs, rng, first, dist):
        """Best score of `first` followed by any second move, then greedy."""
        self._put(gs, rng)
        gold0 = State(gs).gold
        r, t1 = self._macro(first)
        if r == PLAY_WON:
            return 1e9
        if r != PLAY_ON:
            return -1e9 + t1 * 1e6
        mid = self._get()
        mid_gs, mid_rng = mid.raw, ctypes.string_at(self.lib.sim_rng(), 4)
        best = None
        for second in self.macros(mid) if mid.aligned else ["wait"]:
            self._put(mid_gs, mid_rng)
            r, t2 = self._macro(second)
            if r == PLAY_WON:
                v = 1e9
            elif r != PLAY_ON:
                v = -1e9 + (t1 + t2) * 1e6
            else:
                v = self._finish(dist, gold0, t1 + t2)
            best = v if best is None else max(best, v)
        return best

    def _macro_done(self, s, standing):
        p = self.plan
        if p["frames"] >= 40:
            return True
        if p["move"] == "wait":
            return p["frames"] >= 6
        if p["move"] in ("tl", "tr"):
            return p["dug"] and standing
        return standing and (s.gx, s.gy) != p["cell"]

    def decide(self, gs, rng):
        """The IN_* bits to hold until the next look at the game."""
        s = State(gs)
        if s.state != 1:
            self.plan = None
            return 0
        standing = s.aligned and s.pstate not in (PS_FALLING, PS_DIGGING)
        if self.plan and not self._macro_done(s, standing):
            p = self.plan
            p["frames"] += 1
            if s.pstate == PS_DIGGING:
                p["dug"] = True
            m = p["move"]
            if m == "wait" or s.pstate == PS_FALLING:
                return 0
            if m in ("tl", "tr"):
                d = -1 if m == "tl" else 1
                if p["dug"] or s.tile(s.gx + d, s.gy + 1) != BRICK:
                    p["dug"] = True
                    return 0
                return IN_FIRE | (IN_LEFT if d < 0 else IN_RIGHT)
            return self.keys_for(s, m)
        self.plan = None
        if not standing:
            return 0
        dist = Grid(s.tile).dist_to(self.goals(s))
        best = max(self.macros(s), key=lambda m: self._value(gs, rng, m, dist) - (30 if m == "wait" else 0))
        self.decisions += 1
        self.plan = {"move": best, "cell": (s.gx, s.gy), "frames": 0, "dug": False}
        return self.decide(gs, rng)

    @staticmethod
    def key_names(bits):
        return " ".join(name for bit, name in KEYS.items() if bits & bit)
