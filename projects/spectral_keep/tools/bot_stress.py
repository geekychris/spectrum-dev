#!/usr/bin/env python3
"""The test's bot against uneven frame timing, on the host sim: between two
looks at the game the logic runs 1, 2 or 3 steps (a slow frame on the Next
runs the logic more than once), from a different random pattern each run.
The bot must take the relic and finish the game every time.

    python3 tools/bot_stress.py [runs]
"""
import os
import random
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from bot import Bot, HostSim  # noqa: E402

runs = int(sys.argv[1]) if len(sys.argv) > 1 else 50
sim_exe = os.path.join(os.path.dirname(HERE), "build", "host", "sim")
fails = 0
for seed in range(runs):
    rnd = random.Random(seed)
    sim, bot = HostSim(sim_exe), Bot()
    p = sim.read()
    for _ in range(6000):
        held, pressed = bot.step(p)
        for i in range(rnd.choice((1, 1, 2, 2, 3))):
            sim.step(held, pressed if i == 0 else 0)
            p = sim.read()
        if bot.phase == "done":
            break
    sim.close()
    if bot.phase != "done" or not any("pickup R hall" in line for line in sim.log):
        fails += 1
        print(f"  seed {seed}: {bot.phase}, relic {'yes' if any('pickup R' in l for l in sim.log) else 'no'}")
print(f"{runs - fails}/{runs} runs took the relic and finished")
sys.exit(1 if fails else 0)
