"""Play the repaired levels to the exit with this port's rules.

Each level is loaded by the port's own code built for the host
(tools/hostsim.c: player.c, level.c, play.c and the packed levels_data.c),
the guards taken out, and the route search's moves (tools/bdlevels.py, the
same search tools/repair_levels.py used in the original: the gold in a
winning order, then row 0 with the hidden ladders shown) are played frame by
frame through play_tick until it says PLAY_WON.

    python3 tools/check_levels.py            # all 150
    python3 tools/check_levels.py 1 2 3 -v   # some, with the results
"""
import ctypes
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import bdlevels as bd  # noqa: E402
import bot  # noqa: E402

OFF_ENEMIES, ENEMY_SIZE, ENEMY_ACTIVE, OFF_NUM_ENEMIES, OFF_TILES = 26, 14, 12, 96, 98
KEYS = {"left": bot.IN_LEFT, "right": bot.IN_RIGHT, "up": bot.IN_UP, "down": bot.IN_DOWN, "fall": 0}


def sim():
    bot.build_sim()
    lib = ctypes.CDLL(str(bot.SO))
    lib.sim_gs.restype = ctypes.c_void_p
    lib.sim_tick.restype = ctypes.c_uint8
    return lib


def play(lib, lv):
    """True when level lv is won; else the reason"""
    lib.sim_load(lv)
    size = lib.sim_gs_size()
    gs = bytearray(ctypes.string_at(lib.sim_gs(), size))
    for i in range(gs[OFF_NUM_ENEMIES]):                   # no guards
        gs[OFF_ENEMIES + ENEMY_SIZE * i + ENEMY_ACTIVE] = 0
    gs[OFF_NUM_ENEMIES] = 0
    ctypes.memmove(lib.sim_gs(), bytes(gs), size)
    lib.sim_sync()
    s = bot.State(gs)
    g = [[s.tile(x, y) for x in range(bd.W)] for y in range(bd.H)]
    g[s.gy][s.gx] = bd.P                                   # (the start marker, for the search)
    order, brk = bd.plan(g)
    if order is None:
        return f"no route ({brk[0]} at {brk[1]})"
    for kind, (tx, ty) in bd.route(g, order):
        for _ in range(60):
            r = lib.sim_tick(KEYS[kind])
            if r == bot.PLAY_WON:
                return True
            if r != bot.PLAY_ON:
                return f"event {r} going {kind} to {(tx, ty)}"
            p = bot.State(ctypes.string_at(lib.sim_gs(), size))
            if (p.gx, p.gy) == (tx, ty) and p.aligned:
                break
        else:
            return f"stuck going {kind} to {(tx, ty)}"
    for _ in range(30):
        if lib.sim_tick(bot.IN_UP) == bot.PLAY_WON:
            return True
    return "the route ended without the win"


def main():
    only = [int(a) for a in sys.argv[1:] if a.isdigit()] or list(range(1, 151))
    lib = sim()
    won = 0
    for lv in only:
        r = play(lib, lv)
        won += r is True
        if r is not True or "-v" in sys.argv:
            print(f"level {lv:3d}: {'won' if r is True else r}")
    print(f"{won} of {len(only)} levels won with the port's rules (guards out)")
    sys.exit(0 if won == len(only) else 1)


if __name__ == "__main__":
    main()
