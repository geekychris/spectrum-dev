"""Play levels with the test bot on the host build of the rules (no
emulator): which levels the bot clears, and in how many frames.

    python3 tools/try_levels.py [first [last]] [--nearest] [--no-guards] [--horizon=F] [-j N]

The bot (tools/bot.py) takes the gold in the order the route search
(tools/bdlevels.py) found winnable, then heads for the exit; --nearest makes
it go for the nearest gold instead (as in ORIGINAL_BUGS.md's first count).
--no-guards takes the guards out (what the bot can do on the layouts alone).
--horizon: how far ahead the bot looks, in frames (default 150; the test's
games use 90).
Each level gets 6,000 frames (2 minutes). -j runs N processes; the last
line sums up.
"""
import ctypes
import os
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import bdlevels as bd  # noqa: E402
from bot import Bot, State, PLAY_ON, PLAY_WON, PLAYER_M  # noqa: E402

argv = sys.argv[1:]
if "-j" in argv:
    i = argv.index("-j")
    jobs = int(argv[i + 1])
    argv = argv[:i] + argv[i + 2:]
args = [a for a in argv if not a.startswith("-")]
flags = [a for a in argv if a.startswith("-")]
first = int(args[0]) if args else 1
last = int(args[1]) if len(args) > 1 else (first if args else 150)


def summary(lines):
    n = len(lines)
    won = sum("CLEARED" in l for l in lines)
    died = sum("died" in l for l in lines)
    print(f"{won} of {n} cleared, {died} caught (or buried), {n - won - died} out of time")


if "-j" in sys.argv:                       # split the range over N processes
    rest = flags
    span = list(range(first, last + 1))
    parts = [span[i::jobs] for i in range(jobs)]
    procs = [subprocess.Popen([sys.executable, __file__, "--levels=" + ",".join(map(str, p))] + rest,
                              stdout=subprocess.PIPE, text=True) for p in parts if p]
    lines = sorted((l for p in procs for l in p.communicate()[0].splitlines() if l.startswith("level")),
                   key=lambda l: int(l.split()[1].rstrip(":")))
    print("\n".join(lines))
    summary(lines)
    sys.exit(0)

levels = range(first, last + 1)
for f in flags:
    if f.startswith("--levels="):
        levels = [int(v) for v in f.split("=", 1)[1].split(",")]
horizon = next((int(f.split("=")[1]) for f in flags if f.startswith("--horizon=")), 150)
bot = Bot(horizon=horizon)
lib = bot.lib
lines = []
for lv in levels:
    lib.sim_load(lv)
    gs = bytearray(ctypes.string_at(lib.sim_gs(), bot.size))
    if "--no-guards" in flags:
        for i in range(gs[96]):
            gs[26 + 14 * i + 12] = 0
        gs[96] = 0
        ctypes.memmove(lib.sim_gs(), bytes(gs), bot.size)
        lib.sim_sync()
    s = State(bytes(gs))
    g = [[s.tile(x, y) for x in range(bd.W)] for y in range(bd.H)]
    g[s.gy][s.gx] = PLAYER_M
    bot.order = None if "--nearest" in flags else bd.plan(g)[0]
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
    line = (f"level {lv:3d}: {what:8s} frame {frame:5d} gold {s.gold}/{s.gold_total} at {s.gx},{s.gy}"
            f"  ({time.time() - t0:.1f}s)")
    lines.append(line)
    print(line, flush=True)
if len(lines) > 1 and not any(f.startswith("--levels=") for f in flags):
    summary(lines)
