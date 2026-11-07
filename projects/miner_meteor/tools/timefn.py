"""T-states each call of some functions takes, in the demo's cavern N:
breakpoint at the entry, then at the return address.
python3 tools/timefn.py 6 _cs_update _game_tick ..."""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(HERE.parents[1] / "tools" / "zxdev" / "src"))
from zxdev.emu import Emulator  # noqa: E402

FRAME = 69888


def now(e):
    p = e.position()
    return p["frame"] * FRAME + p["tstates"]


with Emulator() as e:
    e.load(HERE / "build" / "miner_meteor.tap")
    e.run_until("miner: demo start", 2000)
    e.run_until(f"state=playing level={sys.argv[1]}", 4000)
    e.run(20)
    for fn in sys.argv[2:]:
        res = []
        for _ in range(10):
            e.break_at(fn)
            if not e.continue_(10)["stopped"]:
                e.delete()
                break
            ret = int.from_bytes(e.read_mem(e.regs()["SP"], 2), "little")
            t0 = now(e)
            e.delete()
            e.break_at(ret)
            e.continue_(10)
            res.append(now(e) - t0)
            e.delete()
        print(f"{fn:22} {res}")
