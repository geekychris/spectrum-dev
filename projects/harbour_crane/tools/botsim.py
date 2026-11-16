#!/usr/bin/env python3
"""tools/bot.py against the host build of the rules (tools/sim.c): one
container loaded by hand, then three dropped past the stern (game over),
as test.py plays it on the Next.

    cc -O2 -shared -fPIC -Isrc -I../../sdk/include -o build/host/libhc.so tools/sim.c src/hc_core.c \\
       ../../sdk/src/fix16.c ../../sdk/src/fix16_tab.c
    python3 tools/botsim.py
"""
import ctypes
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from bot import PROBE_SIZE, Bot, decode  # noqa: E402

NAMES = ["NONE", "GRAB", "RELEASE", "LAND", "DENT", "LOST", "BUMP", "SPLASH", "CLEAR", "LEVEL", "CAPSIZE", "TIMEOUT",
         "OVER", "WIN", "GUST"]
lib = ctypes.CDLL(os.path.join(HERE, "..", "build", "host", "libhc.so"))
probe = (ctypes.c_uint8 * PROBE_SIZE).in_dll(lib, "hc_probe")


def run(tasks, seed=1, level=1, limit=20000):
    lib.sim_new(seed, level)
    bot = Bot()
    log = []
    t = 0
    for task in tasks:
        bot.task(*task)
        while not bot.done and t < limit:
            lib.probe()
            ev = lib.sim_tick(bot.step(decode(probe)))
            t += 1
            for k, n in enumerate(NAMES):
                if ev & (1 << k) and n not in ("BUMP", "SPLASH", "GUST", "RELEASE"):
                    log.append((t, n))
        for _ in range(40):                          # the box lands
            lib.probe()
            ev = lib.sim_tick(0)
            t += 1
            for k, n in enumerate(NAMES):
                if ev & (1 << k) and n not in ("BUMP", "SPLASH", "GUST", "RELEASE"):
                    log.append((t, n))
    lib.probe()
    return log, decode(probe)


if __name__ == "__main__":
    log, p = run([("load", 2, 0), ("dump",), ("dump",), ("dump",)])
    print(log)
    print(p)
