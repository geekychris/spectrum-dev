#!/usr/bin/env python3
"""The port's logic against the original's: the bot plays on the port's host
sim (build/host/sim), its inputs are recorded, then the same inputs go to
the original (build/host/orig_sim, Q12). Prints both event logs side by
side and how far the player's position drifts (in tiles).

    python3 tools/compare.py
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from bot import Bot, HostSim  # noqa: E402

BUILD = os.path.join(os.path.dirname(HERE), "build", "host")


def events(log):
    return [l for l in log if l.split()[0] in ("pickup", "death", "room", "gate", "throne:", "level", "talk")]


def main():
    sim = HostSim(os.path.join(BUILD, "sim"))
    bot = Bot()
    inputs, port_pos = [], []
    p = sim.read()
    while bot.phase != "done" and len(inputs) < 8000:
        held, pressed = bot.step(p)
        inputs.append((held, pressed))
        sim.step(held, pressed)
        p = sim.read()
        port_pos.append((p["state"], p["room"], p["px"], p["py"], p["pz"]))
    sim.close()

    o = subprocess.run([os.path.join(BUILD, "orig_sim")], input="".join(f"{h} {q}\n" for h, q in inputs) + "q\n",
                       capture_output=True, text=True).stdout.splitlines()
    olog = [l[2:] for l in o if l.startswith("L ")]
    opos = [l.split()[1:] for l in o if l.startswith("X ")][1:]
    worst = 0.0
    for (st, rm, x, y, z), (ost, orm, ox, oy, oz) in zip(port_pos, opos):
        if st == 1 and int(ost) == 1 and rm == int(orm):
            worst = max(worst, abs(x - int(ox) / 4096), abs(y - int(oy) / 4096), abs(z - int(oz) / 4096))
    pe, oe = events(sim.log), events(olog)
    print(f"{len(inputs)} steps; port events {len(pe)}, original {len(oe)}; worst position difference {worst:.3f} tiles")
    for a, b in zip(pe + [""] * (len(oe) - len(pe)), oe + [""] * (len(pe) - len(oe))):
        print(f"  {'  ' if a == b else '!='} {a:45s} | {b}")
    return 0 if pe == oe else 1


if __name__ == "__main__":
    sys.exit(main())
