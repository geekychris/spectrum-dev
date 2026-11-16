"""Terra Strike (Next) - plays through the harness (`./zx test terra_strike`).

On the host first: the original's unit tests (tests/test_cases.c, 67
checks) against the port's simulation, and the island and models made
again from the original's code (tools/gen_terrain.c, tools/gen_models.c)
must be the files checked in.

Then the game: it boots to the title (the 3D flyover, the MOD playing on
TurboSound, S turns it off and on), a short tap of SPACE starts (latched
through a slow frame). The simulation is checked tick by tick against the
same C built for the host (tools/hoststep.c): at every world_update() the
Z80's World and input go to the host, and the next tick's World must be
the host's (the event queue main.c empties aside), over 900 ticks of
flying, climbing, diving, turning and firing. tools/tsbot.py then flies the
craft from the world's state and must destroy targets; the frame rate comes
from the game's own `ts: perf` lines. P pauses, BREAK goes back to the
title. Finally three dives into the ground end the game. The simulation's
code (bank 22, page 46) must be unchanged at the end: main.c once reset
render.c's counters with the simulation's bank mapped, over aim_target.
Screenshots go to screenshots/.
"""
import re
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / "tools" / "zxdev" / "src"))
sys.path.insert(0, str(HERE / "tools"))
from zxdev.emu import Emulator  # noqa: E402
from tsbot import GS, Pilot, state  # noqa: E402

NEX = HERE / "build" / "terra_strike.nex"
SHOTS = HERE / "screenshots"
TRACE = HERE / "build-trace"
WORLD_SIZE = 4634                     # sizeof(World) (the host's packed build says so too)
EV_FROM, EV_TO = 4494, 4592           # World.ev[] and nev: main.c pops the events between ticks
LOGIC_PAGE = 44                       # bank 22 (world.c) at 0x0000
failures = []


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}" + (f"  ({detail})" if detail and not ok else ""))
    if not ok:
        failures.append(name)


def cc(out, *srcs):
    subprocess.run(["cc", "-O1", "-w", "-Isrc", "-o", str(out), *srcs], cwd=HERE, check=True)


# ---------------------------------------------------------------- the host

print("host:")
TRACE.mkdir(exist_ok=True)
cc(TRACE / "tests", "tests/host_main.c", "tests/test_cases.c", "src/world.c", "src/world_setup.c",
   "src/terrain.c", "src/ts_math.c", "src/ts_cam.c")
r = subprocess.run([str(TRACE / "tests")], capture_output=True, text=True)
m = re.search(r"(\d+) checks, (\d+) failed", r.stdout)
check("the original's unit tests pass on the port's simulation", r.returncode == 0 and m and m.group(2) == "0",
      r.stdout[-300:])
print(f"    {m.group(0) if m else '?'}")
with tempfile.TemporaryDirectory() as t:
    t = Path(t)
    shutil.copytree(HERE / "src", t / "src")
    cc(TRACE / "gen_terrain", "tools/gen_terrain.c", "src/terrain.c", "src/ts_math.c")
    cc(TRACE / "gen_models", "tools/gen_models.c", "src/models.c", "src/ts_math.c")
    for tool in ("gen_terrain", "gen_models"):
        subprocess.run([str(TRACE / tool)], cwd=t, check=True, capture_output=True)
    same = all((t / "src" / f).read_bytes() == (HERE / "src" / f).read_bytes()
               for f in ("island.asm", "island.h", "models_data.c"))
    check("the island and the models are the original's code's (made again on the host)", same)
cc(TRACE / "hoststep", "tools/hoststep.c", "src/world.c", "src/world_setup.c", "src/terrain.c", "src/ts_math.c")


def code_pages(e):
    return e.read_mem(0, 8192, page=LOGIC_PAGE) + e.read_mem(0, 8192, page=LOGIC_PAGE + 1) + \
        e.read_mem(0, 8192, page=46)


def perf(lines):
    return [int(x) / 10 for x in re.findall(r"ts: perf fps10=(\d+)", "\n".join(lines))]


# ---------------------------------------------------------------- the game

with Emulator(machine="next") as e:
    print("Next:")
    e.load(NEX)
    check("boots to the title", e.run_until("ts: ready", 2000))
    e.run(1)
    ready = e.debug_log[-1]
    check("at 28 MHz", "turbo=3" in ready, ready)
    code0 = code_pages(e)
    e.run(150)
    SHOTS.mkdir(exist_ok=True)
    e.screenshot(SHOTS / "title.png")
    t0 = e.read_mem("snd_music_ticks", 2)
    e.run(100)
    t1 = e.read_mem("snd_music_ticks", 2)
    played = struct.unpack("<H", t1)[0] - struct.unpack("<H", t0)[0]
    check("the music plays behind the title", played >= 90, f"{played} ticks in 100 frames")
    e.press("S", hold_frames=12)
    check("S turns it off", e.run_until("ts: music 0", 100))
    e.run(30)
    e.press("S", hold_frames=12)
    check("and on again", e.run_until("ts: music 1", 100))

    e.press("SPACE", hold_frames=3)
    check("a short tap of SPACE starts (latched)", e.run_until("ts: STATE PLAY", 150))
    s = state(e)
    check("on the pad, landed, 3 lives, 9 targets", s["craft"] == 0 and s["lives"] == 3 and s["targets"] == 9, str(s))

    # ---- lockstep: every tick's World against the host's
    host = subprocess.Popen([str(TRACE / "hoststep")], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
    wu = e.addr("world_update") & 0xFFFF
    e.break_at(wu)
    plan = ["7", "7+A", "7+A+SPACE", "8+A+SPACE", "6+SPACE", "5+A+SPACE", "SPACE", "7+8+A", "Z+SPACE",
            "6+5", "7+SPACE", "A+8+SPACE", "SPACE", "7+5", "Z", ""]
    prev, ticks, bad = None, 0, []
    e.release()
    e.hold(plan[0])
    while ticks < 900 and len(bad) < 3:
        r = e.continue_(300)
        if not r["stopped"]:
            bad.append(f"no tick in 300 frames at tick {ticks}")
            break
        if int(r["regs"]["MMU"][:4], 16) != LOGIC_PAGE:
            continue                          # another bank's code at that address
        sp = r["regs"]["SP"]
        wp, inp = struct.unpack("<HH", e.read_mem(sp + 2, 4))
        wb = e.read_mem(wp, WORLD_SIZE)
        if prev is not None:
            host.stdin.write(prev[0] + struct.pack("<H", prev[1]))
            host.stdin.flush()
            exp = host.stdout.read(WORLD_SIZE)
            exp = exp[:EV_FROM] + wb[EV_FROM:EV_TO] + exp[EV_TO:]
            if exp != wb:
                bad.append(f"tick {ticks}: bytes {[i for i in range(WORLD_SIZE) if exp[i] != wb[i]][:8]}")
        prev = (wb, inp)
        ticks += 1
        if ticks % 60 == 0:
            e.release()
            k = plan[(ticks // 60) % len(plan)]
            if k:
                e.hold(k)
    e.delete()
    e.release()
    host.stdin.close()
    host.wait()
    check(f"the simulation is the host build's, tick for tick ({ticks} ticks)", not bad and ticks >= 900,
          "; ".join(bad))

    # ---- the pilot: back on the pad (BREAK, start again), then fight
    e.hold("CAPS+SPACE")
    e.run(12)
    e.release()
    check("BREAK goes back to the title", e.run_until("ts: back to title", 100))
    e.run(20)
    e.press("SPACE")
    e.run_until("ts: STATE PLAY", 150)
    mark = len(e.debug_log)
    pilot = Pilot(e)
    shot = False
    for i in range(500):
        s = pilot.step(4)
        if not shot and s["score"] >= 100 and s["craft"] == 1:
            e.run(12)                         # the HUD shows the score
            e.screenshot(SHOTS / "play.png")
            shot = True
        if s["score"] >= 400 or s["state"] != 1:
            break
    pilot.keys(None)
    log = e.debug_log[mark:]
    kills = [ln for ln in log if "ts: KILL" in ln]
    print(f"    {len(kills)} kills, score {state(e)['score']}, {i + 1} looks")
    check("lifts off", any("ts: TAKEOFF" in ln for ln in log))
    check("the pilot destroys targets", len(kills) >= 3, f"{kills}")
    fps = perf(log)
    if fps:
        print(f"    fps while flying: {min(fps)} .. {max(fps)}, mean {sum(fps) / len(fps):.1f} ({len(fps)} samples)")
    check("about 4 fps while flying (2.5 or more)", fps and min(fps) >= 2.5 and sum(fps) / len(fps) >= 3.5, f"{fps}")

    # ---- pause
    e.press("P", hold_frames=12)
    e.run(20)
    s0 = state(e)
    e.run(100)
    s1 = state(e)
    e.screenshot(SHOTS / "paused.png")
    check("P pauses", (s0["x"], s0["z"], s0["y"]) == (s1["x"], s1["z"], s1["y"]))
    e.press("P", hold_frames=12)
    e.run(40)

    # ---- three dives into the ground
    for n in range(3):
        s = state(e)
        for _ in range(100):
            s = state(e)
            if s["craft"] == 1 or s["state"] != 1:
                break
            e.hold("7")
            e.run(10)
        e.release()
        e.hold("7+A")                         # up and away from the pad (a dive onto it lands)
        e.run(60)
        e.release()
        e.hold("6")
        crashed = e.run_until("ts: CRASH", 1500)
        e.release()
        if not crashed:
            print("    no crash:", state(e))
            break
        if n < 2:
            e.run_until("ts: RESPAWN", 1500)
            e.run(10)
    check("three crashes end the game", e.run_until("ts: GAME OVER", 2000))
    e.run(100)
    e.screenshot(SHOTS / "gameover.png")
    check("game over", GS[state(e)["state"]] == "GAMEOVER")
    check("the simulation's code is unchanged", code_pages(e) == code0)
    if failures:
        print("\n".join(e.debug_log[-15:]))

print(f"{'FAIL' if failures else 'PASS'}: terra_strike")
sys.exit(1 if failures else 0)
