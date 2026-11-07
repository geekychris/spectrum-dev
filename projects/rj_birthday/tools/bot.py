"""The test bot's view of RJ's Birthday Bash: GameState read from the
Next's memory, and walking RJ towards a point. Used by test.py and
tools/make_gif.py.

GameState (src/game.h) as zsdcc lays it out: no padding, so the offsets
are a host build's with -fpack-struct=1 (its size, 1404, is the map's).
"""
import re
import struct
from pathlib import Path

# GameState (src/game.h) as zsdcc lays it out
GS_SIZE = 1404
O_PLAYER, O_GUESTS, O_ITEMS, O_SCORE, O_LIVES, O_CLOCK, O_STATE = 0, 16, 416, 672, 676, 678, 680
O_CAMERA, O_ROOM, O_COPS, O_HI_NAMES, O_HI_SCORES = 682, 686, 1212, 1294, 1354
ITEM_SIZE, COP_SIZE, MAX_ITEMS, MAX_COPS = 16, 22, 16, 3
ST_TITLE, ST_PLAYING, ST_GAMEOVER, ST_HISCORE, ST_ENTER_NAME, ST_JAIL = 0, 1, 2, 5, 6, 10
ROOM_AMSTERDAM, ITEM_PLANT = 1, 8


def gs_addr(map_path):
    """GameState's address, from the link map"""
    m = re.search(r"^_gs\s+= \$([0-9A-F]+)", Path(map_path).read_text(), re.M)
    return int(m.group(1), 16)


def w(b, o):
    return struct.unpack_from("<h", b, o)[0]


def read_gs(e, addr):
    b = e.read_mem(addr, GS_SIZE)
    g = {"x": w(b, O_PLAYER), "y": w(b, O_PLAYER + 2), "score": struct.unpack_from("<i", b, O_SCORE)[0],
         "lives": w(b, O_LIVES), "state": w(b, O_STATE), "camera": w(b, O_CAMERA), "room": w(b, O_ROOM),
         "plants": [], "cops": []}
    for i in range(MAX_ITEMS):
        o = O_ITEMS + i * ITEM_SIZE
        if w(b, o) and w(b, o + 2) == ITEM_PLANT and w(b, o + 12) == ROOM_AMSTERDAM:
            g["plants"].append((w(b, o + 4), w(b, o + 6)))
    for i in range(MAX_COPS):
        o = O_COPS + i * COP_SIZE
        if w(b, o):
            g["cops"].append((w(b, o + 2), w(b, o + 4)))
    g["hi"] = [(b[O_HI_NAMES + i * 12:O_HI_NAMES + i * 12 + 12].split(b"\0")[0].decode("latin1"),
                struct.unpack_from("<i", b, O_HI_SCORES + i * 4)[0]) for i in range(5)]
    return g


def walk(e, g, tx, ty, frames=3):
    """hold the keys towards (tx, ty) for a few frames"""
    keys = []
    if tx > g["x"] + 2:
        keys.append("P")
    elif tx < g["x"] - 2:
        keys.append("O")
    if ty > g["y"] + 2:
        keys.append("A")
    elif ty < g["y"] - 2:
        keys.append("Q")
    if keys:
        e.hold("+".join(keys))
    e.run(frames)
    e.release()
