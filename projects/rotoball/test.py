"""Rotoball (Next) - plays through the harness.

Boots to the title (music on two TurboSound chips), checks the menu with
the Kempston and the Sinclair 2 joysticks, then, recorded from reset:
a one-player match against the CPU with tools/bot.py driving P1's keys
from the game's probe (src/game.c) every frame - it lets the CPU score,
then tackles it and carries the ball into the CPU's goal - to full time;
back to the title; a two-player match where P1 drives on the keys and P2 on
a Sinclair 1 joystick, each pane following its own skimmer; BREAK, back to
the title. The recording is replayed twice: every frame hash must match.
Screenshots go to screenshots/, the frame rate is reported.
"""
import re
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / "tools" / "zxdev" / "src"))
sys.path.insert(0, str(HERE / "tools"))

from zxdev.emu import Emulator  # noqa: E402
from bot import C_BACK, C_FWD, C_LEFT, C_RIGHT, ONE, PROBE_SIZE, Bot, decode  # noqa: E402

NEX = HERE / "build" / "rotoball.nex"
SHOTS = HERE / "screenshots"
REC = HERE / "build" / "bot-game.json"
P1_KEYS = [(C_FWD, "W"), (C_BACK, "S"), (C_LEFT, "A"), (C_RIGHT, "D")]
failures = []


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


def probe(e):
    return decode(e.read_mem("roto_probe", PROBE_SIZE))


def bot_match(e):
    """the bot plays P1 until full time; returns the log"""
    bot = Bot()
    down, log, shots = "", "", set()
    for t in range(6000):
        p = probe(e)
        if p["state"] == 2:
            break
        keys = " ".join(k for bit, k in P1_KEYS if bot.step(p) & bit)
        if keys != down:                     # hold() adds to what is held: release first
            e.release()
            if keys:
                e.hold(keys)
            down = keys
        e.run(1)
        log += e.log()
        if "play" not in shots and t == 150:
            e.screenshot(SHOTS / "play.png")
            shots.add("play")
        if "carry" not in shots and p["carrier"] == 0 and bot.phase == "score" and p["p1"][0] > 0:
            e.screenshot(SHOTS / "carry.png")
            shots.add("carry")
        if "cpu" not in shots and p["carrier"] == 1 and p["p2"][0] < -30 * ONE:
            e.screenshot(SHOTS / "cpu_attack.png")
            shots.add("cpu")
    e.release()
    return log


def ay(e):
    """the next 'ay' line (logged every 100 frames): mix0 vol0 mix1 vol1 mix2"""
    mark = len(e.log(0))
    e.run(110)
    m = re.findall(r"ay mix0=(\d+) vol0=(\d+) mix1=(\d+) vol1=(\d+) mix2=(\d+)", e.log(0)[mark:])
    return [int(v) for v in m[-1]] if m else None


SHOTS.mkdir(exist_ok=True)
with Emulator(machine="next") as e:
    e.load(NEX)
    check("boots to the title", e.run_until("roto: ready", 300))
    e.run(80)
    e.screenshot(SHOTS / "title.png")
    a = ay(e)
    check("title music on two TurboSound chips", a and (a[0] & 7) != 7 and (a[2] & 7) != 7 and a[1] + a[3] > 0,
          str(a))
    turbo = e._cmd("get-cpu-turbo-speed").strip()
    check("28 MHz", turbo == "8", turbo)

    e.joystick("Kempston")
    mark = len(e.log(0))
    e.press("DOWN")
    e.press("UP")
    check("Kempston: down picks two players, up one", re.findall(r"sel=(\d)", e.log(0)[mark:]) == ["2", "1"])
    e.joystick("Sinclair 2")
    mark = len(e.log(0))
    e.press("RIGHT")                         # key 2
    e.press("LEFT")                          # key 1
    check("Sinclair 2: right/left pick two/one players", re.findall(r"sel=(\d)", e.log(0)[mark:]) == ["2", "1"])
    e.joystick("Kempston")

    # ---- recorded from reset: a 1P match with the bot, a 2P match, the title
    e.record_input("start", from_reset=True)
    check("boots again (recording)", e.run_until("roto: ready", 300))
    e.run(30)
    e.press("SPACE")
    check("SPACE starts a one-player match", "roto: start players=1" in e.log(0))
    e.run(2)
    a = ay(e)
    check("the music stops for the match", a and a[1] == 0 and a[3] == 0, str(a))
    log = bot_match(e)
    full = e.log(0)
    goals = re.findall(r"goal P(\d) pts=(\d) score=(\d+):(\d+)", full)
    check("the CPU scores first (the bot concedes)", goals and goals[0][0] == "2", str(goals[:3]))
    check("the bot scores", any(g[0] == "1" for g in goals), str(goals))
    check("the bot wins the ball with a tackle", "roto: tackle to=P1" in full)
    m = re.search(r"fulltime score=(\d+):(\d+) players=1", full)
    check("full time after 90 seconds", m is not None)
    p = probe(e)
    if m:
        check("the full-time score is the goals' total",
              (int(m.group(1)), int(m.group(2))) == (sum(g[0] == "1" for g in goals), sum(g[0] == "2" for g in goals))
              and p["score"] == (int(m.group(1)), int(m.group(2))), f"{m.groups()} {goals}")
    e.run(60)
    e.screenshot(SHOTS / "fulltime.png")
    a = ay(e)
    e.press("SPACE")
    check("SPACE: back to the title", e.run_until("roto: title", 30))

    # two players: P1 on W/D, P2 on a Sinclair 1 joystick (6-0)
    e.press("2")
    e.press("SPACE")
    check("two-player match starts", e.run_until("roto: start players=2", 30))
    e.joystick("Sinclair 1")
    p0 = probe(e)
    e.hold("W")
    e.hold("UP")
    e.run(90)
    p1 = probe(e)
    check("P1's keys drive P1 up the pitch (+X), P2's joystick drives P2 down it (-X)",
          p1["p1"][0] > p0["p1"][0] + 20 * ONE and p1["p2"][0] < p0["p2"][0] - 20 * ONE,
          f"{p0['p1']} {p1['p1']} {p0['p2']} {p1['p2']}")
    e.release()
    e.hold("D")
    e.run(40)
    p2 = probe(e)
    check("P1's strafe moves only P1", p2["p1"][1] < p1["p1"][1] - 5 * ONE and abs(p2["p2"][1] - p1["p2"][1]) < ONE,
          f"{p1['p1']} {p2['p1']} {p1['p2']} {p2['p2']}")
    e.release()
    e.run(80)                                # P1 coasts to a stop
    p2 = probe(e)
    e.hold("RIGHT")                          # P2 (joystick) strafes right: +Z
    e.run(40)
    p3 = probe(e)
    e.release()
    check("P2's strafe moves only P2", p3["p2"][1] > p2["p2"][1] + 5 * ONE and abs(p3["p1"][1] - p2["p1"][1]) < ONE,
          f"{p2['p2']} {p3['p2']}")
    e.screenshot(SHOTS / "two_players.png")
    e.joystick("Kempston")
    e.press("CAPS+SPACE")
    check("BREAK leaves the match: the title", e.run_until("roto: title", 20))
    e.run(20)
    full = e.log(0)
    perf = re.findall(r"worst=(\d+) overruns=(\d+)", full)
    over = struct.unpack("<H", e.read_mem("frame_overruns", 2))[0]
    frames = struct.unpack("<H", e.read_mem("frame_count", 2))[0]
    worst = max(int(w) for w, _ in perf) if perf else 0
    print(f"  ({frames} frames, {over} overruns: {100 * (frames - over) / max(1, frames):.1f}% of frames on time, "
          f"slowest frame {worst} of 312 raster lines)")
    check("50 fps: no frame overruns", over == 0, str(over))
    rec = e.record_input("save", REC)
    print(f"  recorded {rec['frames']} frames, {rec['events']} input changes")

seqs = []
for i in range(2):
    with Emulator(machine="next") as e:
        r = e.replay(REC)
        seqs.append([h for _, h in r["hashes"]])
        check(f"replay {i + 1}: every frame hash matches the recording", r["ok"],
              f"{r.get('mismatches')} mismatches, first at {r.get('first_mismatch')}")
check("two replays give identical screens", seqs[0] == seqs[1] and len(seqs[0]) > 4000)

print(f"{'FAIL' if failures else 'PASS'}: rotoball")
sys.exit(1 if failures else 0)
