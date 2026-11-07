"""Play levels with the test bot on the host build of the rules (no
emulator): which levels the bot clears, and in how many frames.

    python3 tools/try_levels.py [first [last]]
"""
import ctypes
import sys
import time

from bot import Bot, State, PLAY_ON, PLAY_WON

first = int(sys.argv[1]) if len(sys.argv) > 1 else 1
last = int(sys.argv[2]) if len(sys.argv) > 2 else first
bot = Bot()
lib = bot.lib
for lv in range(first, last + 1):
    lib.sim_load(lv)
    bot.plan = None
    t0, r, frame = time.time(), PLAY_ON, 0
    for frame in range(6000):
        gs = ctypes.string_at(lib.sim_gs(), bot.size)
        gs = bytes([1]) + gs[1:]                    # state = playing
        rng = ctypes.string_at(lib.sim_rng(), 4)
        keys = bot.decide(gs, rng)
        ctypes.memmove(lib.sim_gs(), gs, bot.size)
        ctypes.memmove(lib.sim_rng(), rng, 4)
        lib.sim_sync()
        r = lib.sim_tick(keys)
        if r != PLAY_ON:
            break
    s = State(ctypes.string_at(lib.sim_gs(), bot.size))
    what = {PLAY_WON: "CLEARED", PLAY_ON: "timeout"}.get(r, f"died({r})")
    print(f"level {lv:3d}: {what:8s} frame {frame:5d} gold {s.gold}/{s.gold_total} at {s.gx},{s.gy}"
          f"  ({time.time() - t0:.1f}s)", flush=True)
