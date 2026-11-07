"""An Emberkeep player for the tests: reads the game's state (`gs`) from the
emulator and chooses the keys, planning with the game's own rules built for
the host (tools/hostsim.c: the same game.c and levels.c).

    bot = Bot()
    bits = bot.decide(gs_bytes)          # INPUT_* bits to hold

On a cell boundary it picks a way to go: the next cell along the shortest
tile path to the nearest rune (the portal once they are in, oil when the
lantern is low), or waiting; each choice is run forward on the host rules
(one cell, then on along the path) and the one that gets furthest without a
death wins. `suicide=True` heads for the nearest hazard instead (the test's
lost lives).
"""
import ctypes
import struct
import subprocess
from collections import deque
from pathlib import Path

HERE = Path(__file__).resolve().parents[1]
SO = HERE / "build" / "hostsim.so"

GRID_W, GRID_H, TILE = 20, 15, 16
T_FLOOR, T_WALL, T_RUNE, T_PORTAL, T_OIL, T_HAZARD = range(6)
GM_TITLE, GM_PLAYING, GM_WIN, GM_LOSE, GM_LEVELWIN = range(5)
LEFT, RIGHT, UP, DOWN, START = 1, 2, 4, 8, 16
KEYS = {LEFT: "O", RIGHT: "P", UP: "Q", DOWN: "A", START: "SPACE"}
STEPS = ((LEFT, -1, 0), (RIGHT, 1, 0), (UP, 0, -1), (DOWN, 0, 1))


def build_sim():
    srcs = [HERE / "src" / "game.c", HERE / "src" / "levels.c", HERE / "tools" / "hostsim.c"]
    if SO.exists() and all(SO.stat().st_mtime > s.stat().st_mtime for s in srcs + [HERE / "src" / "game.h"]):
        return
    SO.parent.mkdir(exist_ok=True)
    subprocess.run(["cc", "-O2", "-shared", "-fPIC", "-I", str(HERE / "src"), *map(str, srcs), "-o", str(SO)],
                   check=True)


class State:
    """game.h's GameState, packed."""

    def __init__(self, b):
        self.raw = b
        (self.mode, self.level, self.lives, self.runes_left, self.runes_total, self.score, self.lantern,
         self.timer, self.x, self.y, self.facing, _) = struct.unpack_from("<BhhhhIhhhhBB", b, 0)
        self.grid = b[23:23 + GRID_W * GRID_H]
        self.enemies = []
        for i in range(b[419]):
            x, y, dx, dy, lo, hi, axis, colour, alive, frame = struct.unpack_from("<hhhhhhBBBB", b, 323 + 16 * i)
            if alive:
                self.enemies.append((x, y))
        self.tick = struct.unpack_from("<H", b, 424)[0]

    def tile(self, c, r):
        if not (0 <= c < GRID_W and 0 <= r < GRID_H):
            return T_WALL
        return self.grid[r * GRID_W + c]

    @property
    def aligned(self):
        return self.x % TILE == 0 and self.y % TILE == 0

    @property
    def cell(self):
        return (self.x + 8) // TILE, (self.y + 8) // TILE


class Bot:
    def __init__(self, horizon=200, suicide=False):
        build_sim()
        self.lib = lib = ctypes.CDLL(str(SO))
        lib.sim_gs.restype = ctypes.c_void_p
        lib.sim_tick.restype = ctypes.c_uint8
        self.size = lib.sim_gs_size()
        self.horizon = horizon
        self.suicide = suicide
        self.move = 0                   # held until the next cell boundary

    def put(self, gs):
        ctypes.memmove(self.lib.sim_gs(), gs, self.size)

    def get(self):
        return State(ctypes.string_at(self.lib.sim_gs(), self.size))

    def goals(self, s):
        want = {T_HAZARD} if self.suicide else ({T_RUNE} if s.runes_left else {T_PORTAL})
        if not self.suicide and s.lantern < 300:
            want.add(T_OIL)
        return [(c, r) for r in range(GRID_H) for c in range(GRID_W) if s.tile(c, r) in want]

    def field(self, s):
        """Steps to the nearest goal from every cell (walls and, unless it is
        the goal, hazards are out)."""
        goals = self.goals(s)
        dist = {g: 0 for g in goals}
        q = deque(goals)
        while q:
            c, r = q.popleft()
            for _, dc, dr in STEPS:
                n = (c + dc, r + dr)
                t = s.tile(*n)
                if n not in dist and t not in (T_WALL, T_HAZARD):
                    dist[n] = dist[(c, r)] + 1
                    q.append(n)
        return dist

    def greedy(self, s, dist):
        """The step along the field from where the player stands."""
        c, r = s.cell
        best, move = dist.get((c, r), 999), 0
        for bit, dc, dr in STEPS:
            d = dist.get((c + dc, r + dr), 999)
            if d < best:
                best, move = d, bit
        return move

    def rollout(self, gs, first, dist):
        self.put(gs)
        s0 = State(gs)
        move, frames = first, 0
        for t in range(self.horizon):
            s = self.get()
            if t and s.aligned and (t >= 8 or first == 0):
                move = self.greedy(s, dist)
            self.lib.sim_tick(move)
            s = self.get()
            if s.lives < s0.lives or s.mode == GM_LOSE:
                return (-1, t, 0) if not self.suicide else (1, -t, 0)
            if s.level != s0.level or s.mode in (GM_WIN, GM_LEVELWIN):
                return (1, -t, 0)
            if s.runes_left < s0.runes_left and not self.suicide:
                return (1, -t, 0)
        s = self.get()
        return (0, -dist.get(s.cell, 999), 0)

    def decide(self, gs):
        s = State(gs)
        if s.mode != GM_PLAYING:
            self.move = 0
            return 0
        if not s.aligned and self.move:
            return self.move
        dist = self.field(s)
        options = [bit for bit, dc, dr in STEPS if s.tile(s.cell[0] + dc, s.cell[1] + dr) != T_WALL] + [0]
        # ties go to the first: moving before waiting, which only wins when it is safer
        self.move = max(options, key=lambda m: self.rollout(gs, m, dist))
        return self.move

    @staticmethod
    def key_names(bits):
        return " ".join(name for bit, name in KEYS.items() if bits & bit)
