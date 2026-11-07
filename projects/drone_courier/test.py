"""Drone Courier (Spectrum Next) - plays a game through the harness.

Boot -> title (the autopilot flies behind it) -> SPACE starts day 1 -> a
short hand-flown hop (forward and climbing) -> B: the original's autopilot
flies: it collects at the depot and delivers to customers (scores) -> B off:
the drone waits on a roof while the job clock runs out three times (TOO
LATE: three failed jobs end the game) -> game over -> fire -> initials
(BOT) -> back to the title with BOT in the table. Checked on the way: the
original's tests and its long autopilot game's hash on the Next (hold T at
start-up), the frame rate and the logic's 25 ticks a second, the sound on
the TurboSound, the help pages and the pause. The start of the game (to
the first delivery) is recorded and replayed twice: every frame hash must
match. Screenshots go to projects/drone_courier/screenshots/.
"""
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "zxdev" / "src"))

from zxdev.emu import Emulator  # noqa: E402
from PIL import Image  # noqa: E402

sys.path.insert(0, str(Path(__file__).resolve().parent / "tools"))
import refview  # noqa: E402  (the original's view in Python)

HERE = Path(__file__).resolve().parent
NEX = HERE / "build" / "drone_courier.nex"
SHOTS = HERE / "screenshots"
REC = HERE / "build" / "bot-game.json"
failures = []
CS_TITLE, CS_PLAY, CS_OVER, CS_DONE = range(4)


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


def probe(e):
    b = e.read_mem("_dc_probe", 14 + 12 + 6)
    f = struct.unpack("<14BiiIhhh", b)
    keys = ("state day jobs jobs_done fails carrying landed autoplay help paused between over battery goal "
            "score job_time tick x y z").split()
    return dict(zip(keys, f))


def settle(e):
    """the frame drawn: stop where the main loop waits for the next frame, then
    let the emulator finish that frame (a screenshot taken at the breakpoint
    shows the rows below the beam from the frame before)"""
    e.break_at("_frame_wait")
    e.continue_(10)
    e.delete()
    e.run(1)


def shot(e, name):
    settle(e)
    return e.screenshot(SHOTS / f"{name}.png")


def view_matches(e):
    """the picture just drawn (the back buffer, at frame_wait) against the
    original's scene() for the same state (refview.py), at the Next's scale;
    returns the share of pixels that differ. The drone is sprites, not in
    Layer 2: its pixels are left out (the reference drawn without it)"""
    from PIL import Image
    e.break_at("_frame_wait")
    e.continue_(30)
    e.delete()
    g = refview.parse_dcgame(e.read_mem("_game", 400))
    cam_yaw = struct.unpack("<H", e.read_mem("_co", 42)[40:42])[0]
    back = e.peek("_l2_back")
    raw = b"".join(e.read_mem(0, 16384, bank=back + i) for i in range(3))
    nx = Image.frombytes("P", (256, 192), raw).crop((0, 0, 256, 96))
    ref, _ = refview.render(g, cam_yaw, scale=0.8, rows=96, half=True, drone=False)
    a, b = nx.tobytes(), ref.tobytes()
    same = sum(1 for p, q in zip(a, b) if p == q or (p >= 32 and q == refview.PEN_SKY))
    return 1 - same / len(a)


def run_until_log(e, pattern, frames, step=25):
    """run until the log matches; returns (match or None, the log read)"""
    log = ""
    for _ in range(0, frames, step):
        e.run(step)
        log += e.log()
        m = re.search(pattern, log)
        if m:
            return m, log
    return None, log


def perf_of(log, state):
    rows = [tuple(map(int, m)) for m in
            re.findall(r"dc: perf draws=(\d+) frames=(\d+) ticks=(\d+) polys=(\d+) towers=(\d+) state=" + state, log)]
    return rows[1:] if len(rows) > 2 else rows        # the first may straddle a change of state


# ---------------------------------------------------------------- the tests
with Emulator(machine="next") as e:
    # the original's tests on the Next
    e.load(NEX)
    e.hold("T")
    m, log = run_until_log(e, r"dc: selftest (\d+) passed, (\d+) failed", 15000, 50)
    e.release()
    check("the original's tests pass on the Next (and the autopilot game's hash)",
          m is not None and int(m.group(2)) == 0 and int(m.group(1)) >= 30,
          m.group(0) if m else log[-300:])
    for f in re.findall(r"dc: FAIL (.*)", log):
        print("    failed:", f)

    e.record_input("start", from_reset=True)        # the start of a game, for the replay check
    check("boots to the title", e.run_until("dc: ready", 600))
    e.run(150)
    title = shot(e, "title")
    check("title screen drawn (the city in colour behind it)", len(title.getcolors(1 << 16)) >= 10)
    p = probe(e)
    check("the autopilot flies behind the title", p["state"] == CS_TITLE and p["tick"] > 50, str(p))
    e.press("SPACE")
    check("SPACE starts the game", e.run_until("dc: state=FLY", 60))

    # a short hop by hand: forward and climbing (after the start's SPACE is
    # seen let go: the original's fire lock)
    e.run(20)
    p0 = probe(e)
    e.hold("Q+SPACE")
    e.run(60)
    e.release()
    e.run(10)
    p1 = probe(e)
    check("fire climbs, up flies forward", p1["y"] > p0["y"] + 20 and (p1["x"], p1["z"]) != (p0["x"], p0["z"]),
          f"{p0} -> {p1}")
    shot(e, "flying")
    # l2double.h: on the display each of the 96 drawn rows shows on two lines
    # (ZEsarUX shows a copper move a line early: the pairs may start on line 1)
    # (the Next's screenshot is at twice the size; a strip at the left, clear
    # of the HUD's text and the drone's sprites)
    scr = e.screenshot(border=False).convert("RGB")
    scr = scr.resize((scr.width // 2, scr.height // 2), Image.NEAREST).crop((0, 16, 32, 192))
    lines = [scr.crop((0, y, 32, y + 1)).tobytes() for y in range(scr.height)]
    pairs = [sum(lines[y] == lines[y + 1] for y in range(p, scr.height - 1, 2)) for p in (0, 1)]
    check("the copper shows each drawn row twice (l2double)", max(pairs) >= 0.95 * (scr.height // 2 - 1),
          f"{pairs} of {scr.height // 2} line pairs equal")
    d = view_matches(e)
    check("the 3D view is the original's (Layer 2 against tools/refview.py)", d < 0.12, f"{d:.1%} of pixels differ")

    # the autopilot (B) does the deliveries
    e.press("B")
    e.run(5)
    check("B: the autopilot", probe(e)["autoplay"] == 1)
    m, log = run_until_log(e, r"dc: ev DELIVER .*score=(\d+)", 12000, 25)
    check("the autopilot delivers a parcel (it scores)", m is not None and int(m.group(1)) >= 200,
          log[-300:])
    rec = e.record_input("save", REC)
    print(f"  recorded {rec['frames']} frames, {rec['events']} input changes")
    e.run(6)
    shot(e, "delivered")
    full = e.log(0)
    perf = perf_of(full, "FLY")
    if perf:
        draws, frames, ticks = (sum(p[i] for p in perf) for i in range(3))
        fps = 50 * draws / frames
        print(f"  flying: {fps:.1f} fps, logic {50 * ticks / frames:.1f} ticks/s, "
              f"{sum(p[3] for p in perf) / len(perf):.0f} polygons, {sum(p[4] for p in perf) / len(perf):.0f} towers a frame")
        check("logic at the original's 25 ticks a second", abs(50 * ticks / frames - 25) < 0.5,
              f"{ticks}/{frames}")
        check("12+ fps while flying", fps >= 12, f"{fps:.1f}")
    else:
        check("perf lines logged", False)

    # the pickup at the depot with the next parcel
    m, log = run_until_log(e, r"dc: ev PICKUP", 6000, 25)
    check("back at the depot: the next parcel", m is not None)
    e.run(40)
    shot(e, "depot")

    ay = re.findall(r"dc: ay mix0=(\d+) vol0=(\d+) mix2=(\d+) vol2a=(\d+) vol2c=(\d+)", e.log(0))
    check("the tune plays on chip 0 (tone A)", any(int(m0) & 1 == 0 and int(v0) > 0 for m0, v0, *_ in ay), str(ay[:3]))
    check("the rotors hum on chip 2 (channel C)", any(int(m2) & 4 == 0 for _, _, m2, _, _ in ay), str(ay[:3]))

    # help pages, pause
    e.press("H")
    e.run(10)
    check("H: the help pages", probe(e)["help"] == 1)
    shot(e, "help")
    e.press("H")
    e.run(10)
    e.press("H")
    e.run(10)
    check("H again twice: closed", probe(e)["help"] == 0)
    t0 = probe(e)["tick"]
    e.press("ENTER")
    e.run(50)
    p = probe(e)
    check("ENTER pauses", p["paused"] == 1 and p["tick"] == t0 + (p["tick"] - t0) and p["tick"] - t0 <= 2, str(p))
    e.press("ENTER")
    e.run(10)

    # three jobs run out of time: game over
    e.press("B")
    e.run(5)
    check("B again: flying by hand", probe(e)["autoplay"] == 0)
    m, log = run_until_log(e, r"dc: state=OVER", 3 * 3600 + 600, 50)
    lates = re.findall(r"dc: ev LATE", log)
    check("the clock runs out: a job failed (TOO LATE) x3", len(lates) == 3, str(lates))
    check("three failed jobs: game over", m is not None, log[-300:])
    e.run(100)                           # the box takes FIRE after 40 ticks
    shot(e, "gameover")
    final = probe(e)["score"]

    # the sound: the effects on chip 2, the tune on 0 and 1 (in the log's ay lines)
    e.press("SPACE")
    m, more = run_until_log(e, r"dc: hiscore rank|dc: state=TITLE", 300, 5)
    if final > 200:                      # the table's lowest (hs_init(200))
        check("FIRE: high-score entry", m is not None and "hiscore rank" in m.group(0), more[-200:])
        e.run(4)
        shot(e, "initials")
        e.type("BOT")
        e.run(20)
        more += e.log()
        check("initials entered", "dc: name=BOT" in more, more[-200:])
        m, more2 = run_until_log(e, r"dc: state=TITLE", 300, 5)
        more += more2
    check("back to the title", "dc: state=TITLE" in more)
    e.run(250 * 2 + 30)                  # the title's second page: the high scores
    shot(e, "hiscores")

# the start of the game, replayed twice from the recording: identical every frame
seqs = []
for i in range(2):
    with Emulator(machine="next") as e:
        r = e.replay(REC)
        seqs.append([h for _, h in r["hashes"]])
        check(f"replay {i + 1}: every frame hash matches the recorded game", r["ok"],
              f"{r.get('mismatches')} mismatches, first at {r.get('first_mismatch')}")
check("two replays give identical screens", seqs[0] == seqs[1] and len(seqs[0]) > 500)

print(f"{'FAIL' if failures else 'PASS'}: drone_courier")
sys.exit(1 if failures else 0)
