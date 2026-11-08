"""Where the time goes inside one function: from its entry, sample the PC
at random T-states over the next N T-states (several calls).

    .venv/bin/python projects/lander_3d/tools/sample_fn.py _fn T_STATES [samples] [--after TEXT]
"""
import collections
import random
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parents[2] / "tools" / "zxdev" / "src"))
sys.path.insert(0, str(HERE))
from zxdev.emu import Emulator  # noqa: E402
import profile as P  # noqa: E402

fn, span = sys.argv[1], int(sys.argv[2])
n = int(sys.argv[3]) if len(sys.argv) > 3 and not sys.argv[3].startswith("--") else 300
after = sys.argv[sys.argv.index("--after") + 1] if "--after" in sys.argv else "ld: perf"
with Emulator(machine="next") as e:
    e.load(HERE.parent / "build" / "lander_3d.nex")
    e.run_until(after, 30000)
    cnt = collections.Counter()
    per = max(1, n // 6)
    for i in range(6):
        e.break_at(fn)
        e.continue_(400)
        e.delete()
        p = e.position()
        f0, t0 = p["since_load"], p["tstates"]
        rnd = random.Random(i)
        for d in sorted(rnd.randrange(0, span) for _ in range(per)):
            T = t0 + d
            e.run_to(f0 + T // 560000, T % 560000)
            r = e.regs()
            cnt[P.where(r["PC"], r["MMU"])[0]] += 1
    tot = sum(cnt.values())
    for k, v in cnt.most_common(30):
        print(f"{100 * v / tot:5.1f}% {k}")
