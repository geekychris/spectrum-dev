"""The test's player, shared by test.py (the emulator) and the host sim
(tools/sim.c): it reads the game through `keep_probe` (src/probe.c) and
answers with pad bits each logic step.

The plan, in The Gatehouse: start, walk through the gate room's north door
into the Great Hall, round the west side (away from its ghost) to the relic
in the far corner, then stand still until the ghost has taken every life;
game over, back to the title.

    python3 tools/bot.py          # play it on the host sim, print the log
"""
import math
import os
import struct
import subprocess
import sys

FX = 2048
B_UP, B_DOWN, B_LEFT, B_RIGHT, B_A, B_B, B_C, B_P = 1, 2, 4, 8, 16, 32, 64, 128
GS_TITLE, GS_PLAYING, GS_DYING, GS_LEVELDONE, GS_GAMEOVER, GS_VICTORY = range(6)
K_PLAYER, K_GUARD, K_HOUND, K_GHOST, K_BOUNCER, K_SAGE, K_CRATE = range(7)
ROOM_GATE, ROOM_HALL = 0, 3

PROBE_FMT = "<11B3h" + "B3h" * 10 + "4B" * 6 + "H"
PROBE_SIZE = struct.calcsize(PROBE_FMT)


def decode(b):
    v = struct.unpack(PROBE_FMT, bytes(b[:PROBE_SIZE]))
    names = ["state", "level", "room", "lives", "relics", "keys", "paused", "nact", "npk", "player", "ngate"]
    p = dict(zip(names, v[:11]))
    p["px"], p["py"], p["pz"] = (c / FX for c in v[11:14])
    o = 14
    p["act"] = [dict(kind=v[o + 4 * i], x=v[o + 4 * i + 1] / FX, y=v[o + 4 * i + 2] / FX, z=v[o + 4 * i + 3] / FX)
                for i in range(p["nact"])]
    o += 40
    p["pk"] = [dict(kind=chr(v[o + 4 * i]), x=v[o + 4 * i + 1], y=v[o + 4 * i + 2], z=v[o + 4 * i + 3])
               for i in range(p["npk"])]
    p["tick"] = v[o + 24]
    return p


def keys_towards(dx, dz):
    """screen-relative pad bits that walk the world direction (dx, dz):
    up = +x +z, right = +x -z (game.c), 8 directions"""
    best, bits = -2.0, 0
    n = math.hypot(dx, dz)
    if n < 1e-6:
        return 0
    for h in (-1, 0, 1):
        for v in (-1, 0, 1):
            if not h and not v:
                continue
            mx, mz = (h + v), (v - h)
            m = math.hypot(mx, mz)
            d = (mx * dx + mz * dz) / (m * n)
            if d > best:
                best = d
                bits = (B_RIGHT if h > 0 else B_LEFT if h < 0 else 0) | (B_UP if v > 0 else B_DOWN if v < 0 else 0)
    return bits


class Bot:
    """step(probe) -> (held, pressed); `phase` says where it is"""

    ROUTE = {ROOM_GATE: [(4.0, 6.0), (4.0, 9.0)],               # out of the north door
             ROOM_HALL: [(4.0, 1.2), (0.5, 1.2), (0.5, 7.5)]}   # west side to the relic

    def __init__(self):
        self.phase = "title"
        self.wp = 0
        self.room = None
        self.last = 0
        self.wait = 0

    def step(self, p):
        held = 0
        if p["state"] == GS_TITLE:
            if self.phase in ("title",) and p["tick"] > 30:
                self.phase = "start"
                return B_A, B_A
            if self.phase == "over":
                self.phase = "done"
            return 0, 0
        if p["state"] in (GS_GAMEOVER, GS_VICTORY):
            self.phase = "over"
            self.wait += 1
            return (B_A, B_A) if self.wait == 80 else (0, 0)
        if p["state"] != GS_PLAYING:
            return 0, 0
        if self.phase == "start":
            self.phase = "walk"
        if p["room"] != self.room:
            self.room, self.wp = p["room"], 0
        if self.phase == "walk":
            if p["relics"] > 0:
                self.phase = "wait"            # stand still: the ghost does the rest
                return 0, 0
            route = self.ROUTE.get(p["room"], [])
            if self.wp < len(route):
                tx, tz = route[self.wp]
                dx, dz = tx - p["px"], tz - p["pz"]
                if math.hypot(dx, dz) < 0.25 and self.wp < len(route) - 1:
                    self.wp += 1
                    tx, tz = route[self.wp]
                    dx, dz = tx - p["px"], tz - p["pz"]
                held = keys_towards(dx, dz)
        pressed = held & ~self.last
        self.last = held
        return held, pressed


class HostSim:
    def __init__(self, exe):
        self.p = subprocess.Popen([exe], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1)
        self.log = []

    def read(self):
        while True:
            line = self.p.stdout.readline()
            if not line:
                return None
            if line.startswith("L "):
                self.log.append(line[2:].rstrip())
            elif line.startswith("P "):
                return decode(bytes.fromhex(line[2:].strip()))

    def step(self, held, pressed):
        self.p.stdin.write(f"{held} {pressed}\n")

    def close(self):
        self.p.stdin.write("q\n")
        self.p.wait()


def play(sim, bot, max_ticks=6000):
    p = sim.read()
    for t in range(max_ticks):
        held, pressed = bot.step(p)
        sim.step(held, pressed)
        p = sim.read()
        if bot.phase == "done":
            return t
    return None


if __name__ == "__main__":
    here = os.path.dirname(os.path.abspath(__file__))
    sim = HostSim(os.path.join(os.path.dirname(here), "build", "host", "sim"))
    bot = Bot()
    t = play(sim, bot)
    sim.close()
    print("\n".join(sim.log))
    print(f"bot finished: {bot.phase} after {t} ticks")
    sys.exit(0 if bot.phase == "done" else 1)
