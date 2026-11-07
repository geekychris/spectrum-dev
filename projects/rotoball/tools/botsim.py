#!/usr/bin/env python3
"""The bot against the host build of the game's logic (src/game.c):
a one-player match, as test.py plays it on the Next.

    cc -O2 -shared -fPIC -Isrc -I../../sdk/include -o build/host/libroto.so tools/sim.c src/game.c
    python3 tools/botsim.py
"""
import ctypes
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from bot import PROBE_SIZE, Bot, decode  # noqa: E402

lib = ctypes.CDLL(os.path.join(HERE, "..", "build", "host", "libroto.so"))
probe = (ctypes.c_uint8 * PROBE_SIZE).in_dll(lib, "roto_probe")


def match(log=print):
    lib.game_init()
    lib.game_start(1)
    lib.probe_update()
    bot = Bot()
    c = 0
    goals = []
    for t in range(100000):
        lib.game_step(c, 0)
        lib.probe_update()
        p = decode(probe)
        if p["events"] & 4:
            goals.append((t, "P1"))
        if p["events"] & 8:
            goals.append((t, "P2"))
        if p["events"] & 2:
            log(f"{t}: tackle -> {p['carrier']}")
        if p["state"] != 1:
            break
        c = bot.step(p)
    return goals, p


if __name__ == "__main__":
    goals, p = match()
    print(goals, p["score"])
