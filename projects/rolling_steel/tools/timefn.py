"""T-states each call of some functions takes while the attract demo plays
course 1 (breakpoint at the entry, then at the return address).
    .venv/bin/python projects/rolling_steel/tools/timefn.py _phys_marble_step _game_step ..."""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(HERE.parents[1] / "tools" / "zxdev" / "src"))
from zxdev.emu import Emulator  # noqa: E402

FRAME = 69888 * 8


def now(e):
    p = e.position()
    return p["frame"] * FRAME + p["tstates"]


with Emulator(machine="next") as e:
    e.load(HERE / "build" / "rolling_steel.nex")
    e.run_until("demo starts course=1", 2000)
    e.run(150)
    page = None
    for fn in sys.argv[1:]:
        if fn.startswith("page="):          # only stops with this 8K page in slot 0 (banked code)
            page = int(fn[5:])
            continue
        res = []
        for _ in range(24):
            e.break_at(fn)
            ok = False
            for _try in range(50):
                if not e.continue_(10)["stopped"]:
                    break
                if page is None or int(e.regs()["MMU"][0:4], 16) == page:
                    ok = True
                    break
            if not ok:
                e.delete()
                break
            ret = int.from_bytes(e.read_mem(e.regs()["SP"], 2), "little")
            t0 = now(e)
            e.delete()
            e.break_at(ret)
            for _try in range(50):
                e.continue_(10)
                if page is None or int(e.regs()["MMU"][0:4], 16) == page:
                    break
            res.append(now(e) - t0)
            e.delete()
        print(f"{fn:22} {res}  avg {sum(res) // max(1, len(res))}")
