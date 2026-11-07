"""The test bot for Rock Blaster (test.py, make_gif.py): reads the game's
state by symbol and aims at the nearest rock, firing all the time; with
`suicide` it also thrusts at it, so the ship gets hit."""
import math

# GameState (src/game.h; zsdcc packs structs): Ship 25 bytes, 8 Bullets of 14,
# 24 Rocks of 15, 64 Particles of 10, then score, lives, level, state
ROCKS = 25 + 8 * 14
SCORE = ROCKS + 24 * 15 + 64 * 10
GS_SIZE = SCORE + 14
W, H = 320, 240


def state(e):
    b = e.read_mem("_gs", GS_SIZE)
    i16 = lambda o: int.from_bytes(b[o:o + 2], "little", signed=True)
    rocks = []
    for k in range(24):
        o = ROCKS + k * 15
        if b[o + 8]:
            rocks.append((i16(o + 9), i16(o + 11), b[o + 13]))
    return {"x": i16(21), "y": i16(23), "angle": b[16], "alive": b[17], "rocks": rocks,
            "lives": i16(SCORE + 4), "state": b[SCORE + 8]}


def wrapd(d, size):
    return (d + size // 2) % size - size // 2


def keys(s, suicide=False):
    """the keys to hold for the next few frames"""
    out = ["SPACE"]
    if s["alive"] and s["rocks"]:
        dx, dy = min(((wrapd(rx - s["x"], W), wrapd(ry - s["y"], H)) for rx, ry, _ in s["rocks"]),
                     key=lambda r: r[0] ** 2 + r[1] ** 2)
        want = round(math.atan2(dy, dx) * 128 / math.pi) & 255
        diff = (want - s["angle"] + 128) % 256 - 128
        if diff < -2:
            out.append("O")
        elif diff > 2:
            out.append("P")
        if suicide and abs(diff) < 20:
            out.append("Q")
    return "+".join(out)


def step(e, suicide=False, frames=4):
    e.release()
    e.hold(keys(state(e), suicide))
    if frames:
        e.run(frames)
