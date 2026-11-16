"""Terra Strike's test pilot (test.py, tools/make_gif.py): reads `world`
(ts.h's World; the host build packs the struct the same way) and flies the
craft with the keys - lift off, head for the nearest tank, building or
ship, hold height over it and fire when it is ahead (the cannon aims
itself within ~22 degrees of the nose, as the original's)."""
import math
import struct

WORLD = 16384                       # world units (WORLD_SIZE)
GS = ("TITLE", "PLAY", "LEVELCLEAR", "GAMEOVER")
CRAFT = 28                          # offsetof(World, p)
ENTS, ENT_SIZE, MAX_ENTS = 62, 20, 48
TARGETS = (1, 2, 3)                 # ENT_TANK, ENT_BUILDING, ENT_SHIP


def wrapd(a, b):
    d = (a - b) % WORLD
    return d - WORLD if d >= WORLD // 2 else d


def state(e):
    b = e.read_mem("world", ENTS + MAX_ENTS * ENT_SIZE)
    st, level, lives = struct.unpack_from("<hhh", b, 12)
    score, = struct.unpack_from("<i", b, 22)
    targets, = struct.unpack_from("<h", b, 26)
    cs, x, y, z, vy, speed, yaw = struct.unpack_from("<hiiiiih", b, CRAFT)
    fuel, shield = struct.unpack_from("<hh", b, CRAFT + 26)
    ents = []
    for i in range(MAX_ENTS):
        t, hp, eyaw, tm, ex, ey, ez = struct.unpack_from("<hhhhiii", b, ENTS + ENT_SIZE * i)
        if t:
            ents.append((t, ex >> 4, ey >> 4, ez >> 4))
    return dict(state=st, level=level, lives=lives, score=score, targets=targets, craft=cs,
                x=x >> 4, y=y >> 4, z=z >> 4, speed=speed, yaw=yaw, fuel=fuel, shield=shield, ents=ents)


class Pilot:
    def __init__(self, e):
        self.e, self.held = e, None

    def keys(self, k):
        if k != self.held:
            self.e.release()
            if k:
                self.e.hold(k)
            self.held = k

    def step(self, frames=4):
        """one look at the world, keys for the next `frames` frames"""
        s = state(self.e)
        k = []
        if s["state"] == 1 and s["craft"] == 0:          # landed: lift off
            k = ["7"]
        elif s["state"] == 1 and s["craft"] == 1:
            tg = [(math.hypot(wrapd(x, s["x"]), wrapd(z, s["z"])), x, y, z)
                  for t, x, y, z in s["ents"] if t in TARGETS]
            if tg:
                d, x, y, z = min(tg)
                want = math.atan2(wrapd(x, s["x"]), wrapd(z, s["z"])) * 128 / math.pi
                diff = (want - s["yaw"] + 128) % 256 - 128
                if diff > 3:
                    k.append("8")
                elif diff < -3:
                    k.append("5")
                alt = y + (200 if d > 900 else 120)
                if s["y"] < alt - 20:
                    k.append("7")
                elif s["y"] > alt + 40:
                    k.append("6")
                if d > 700 and abs(diff) < 30:
                    if s["speed"] < 200:
                        k.append("A")
                elif s["speed"] > 60:
                    k.append("Z")
                if abs(diff) < 12 and d < 900:
                    k.append("SPACE")
        self.keys("+".join(k) if k else None)
        self.e.run(frames)
        return s
