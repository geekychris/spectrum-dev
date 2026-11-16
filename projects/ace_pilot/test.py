"""Ace Pilot (Spectrum Next) - run by `./zx test ace_pilot`.

1. The rules on the host: tools/trace.sh builds the original's game.c (from
   amiga_mcp/examples/ace_pilot, if it is next to this repo) and the port's
   and compares them tick by tick (skipped without the original).
2. The rules on the Z80: the title's B starts a game flown by the autopilot;
   the Next logs a hash of the game every 1000 ticks, and the same game built
   for the host (tools/hash_host.c: the port's game.c and bot.c) must give
   the same hashes - one player for 6000 ticks, two players for 3000.
3. Playing: the music (M), FIRE starts, O / P turn, W opens the throttle,
   SPACE fires, the HUD, BREAK back to the title, game over (lives set to
   0), the display mode (C). Frame rate from the game's `ace: perf` lines.
Screenshots go to projects/ace_pilot/screenshots/.
"""
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "zxdev" / "src"))

from zxdev.emu import Emulator  # noqa: E402

HERE = Path(__file__).resolve().parent
NEX = HERE / "build" / "ace_pilot.nex"
SHOTS = HERE / "screenshots"
BT = HERE / "build-trace"
failures = []

PLAYER = 34                             # sizeof(PlayerState): Camera 16, score 4, 7 WORDs
YAW, SCORE, LIVES, ALIVE, COOLDOWN, SPEED = 14, 16, 20, 22, 26, 30
# GameWorld.state: players 2 x 34, enemies 8 x 30, ground 16 x 20, bullets 16 x 30,
# particles 32 x 28, then wave, enemies_alive, enemies_to_spawn, spawn_timer
STATE = 2 * PLAYER + 8 * 30 + 16 * 20 + 16 * 30 + 32 * 28 + 8


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


def s16(v):
    return v - 0x10000 if v & 0x8000 else v


# 1. the port's rules against the original's, on the host
orig = Path(ROOT.parent / "amiga_mcp" / "examples" / "ace_pilot")
if shutil.which("cc") and (orig / "game.c").exists():
    r = subprocess.run(["sh", str(HERE / "tools" / "trace.sh")], capture_output=True, text=True,
                       env={"ACE_ORIG": str(orig), "PATH": "/usr/bin:/bin:/usr/local/bin:/opt/homebrew/bin"})
    same = r.stdout.count(": same (")
    check("host: the port's rules = the original's, tick by tick (12 runs of 30000 ticks)",
          r.returncode == 0 and same == 12, r.stdout[-400:] + r.stderr[-400:])
else:
    print("  SKIP host trace against the original (amiga_mcp/examples/ace_pilot not found)")

# the same autopilot game on the host
BT.mkdir(exist_ok=True)
host = BT / "hash_host"
sdk = ROOT / "sdk"
subprocess.run(["cc", "-O1", "-w", "-I", str(HERE / "src"), "-I", str(sdk / "include"), "-o", str(host),
                str(HERE / "tools" / "hash_host.c")]
               + [str(HERE / "src" / f) for f in ("bot.c", "game.c", "colour.c", "trig.c", "models.c")]
               + [str(sdk / "src" / "fix88.c"), str(sdk / "src" / "fix88_tab.c"), "-lm"], check=True)


def host_hashes(players, ticks):
    out = subprocess.run([str(host), str(players), str(ticks)], capture_output=True, text=True, check=True).stdout
    return [line.strip() for line in out.splitlines()]


def next_hashes(e):
    return [m.group(1) for m in re.finditer(r"ace: (hash [^\n]*)", e.log(0))]


def fps_values(e):
    return [int(v) / 10 for v in re.findall(r"ace: perf fps10=(\d+)", e.log(0))]


def player(e, i, off):
    return int.from_bytes(e.read_mem(f"_world+{PLAYER * i + off}", 2), "little", signed=True)


with Emulator(machine="next") as e:
    e.load(NEX)
    check("boots to the title", e.run_until("ace: ready", 2000))
    e.run(10)
    e.screenshot(SHOTS / "title.png")
    check("title drawn", len(e.screenshot().getcolors(1 << 16) or []) >= 6)
    check("the Blue Danube plays (modplay in the interrupt)", e.peek("_music_on") == 1)
    e.press("M")
    check("M: music off", e.run_until("ace: music=0", 20) and (e.run(2) or True) and e.peek("_music_on") == 0)
    e.press("M")
    check("M: music on", e.run_until("ace: music=1", 20))

    # 2. the autopilot, one player, 6000 ticks
    e.press("B")
    check("B starts the autopilot", e.run_until("ace: start players=1 auto=1", 50))
    e.run_until("ace: hash t=3000", 4000)
    e.run(2)
    e.screenshot(SHOTS / "autopilot.png")
    check("autopilot: 6000 ticks", e.run_until("ace: hash t=6000", 4000))
    e.run(1)
    got, want = next_hashes(e), host_hashes(1, 6000)
    check("autopilot: the Z80's game = the host's, every 1000 ticks", got == want[:len(got)] and len(got) == 6,
          f"next {got} host {want}")
    m = re.search(r"s=(\d+),", got[-1]) if got else None
    check("autopilot: it scores", m and int(m.group(1)) >= 1000, got[-1] if got else "")
    fps = fps_values(e)
    if fps:
        print(f"  frame rate, one player (autopilot): {min(fps)}-{max(fps)} fps, "
              f"average {sum(fps) / len(fps):.1f} over {len(fps)} samples")
    check("frame rate: 8 fps or more on average, never below 4", fps and sum(fps) / len(fps) >= 8 and min(fps) >= 4, fps)

    # 3. playing by hand
    e.hold("CAPS SPACE")                # held longer than a slow frame (~8 frames)
    e.run(16)
    e.release()
    check("BREAK goes back to the title", e.run_until("ace: title players=1", 50))
    e.run(20)
    e.press("C")
    check("C: classic display", e.run_until("ace: display=0", 20))
    e.press("C")
    check("C: colour display", e.run_until("ace: display=1", 20))
    e.press("SPACE", hold_frames=3)
    check("FIRE starts a game", e.run_until("ace: start players=1 auto=0", 50))
    e.run(50)
    yaw0, spd0 = player(e, 0, YAW), player(e, 0, SPEED)
    e.hold("P")
    e.run(25)
    e.release()
    yaw1 = player(e, 0, YAW)
    check("P turns right", 0 < (yaw1 - yaw0) % 256 < 128, f"{yaw0} -> {yaw1}")
    e.hold("O")
    e.run(25)
    e.release()
    check("O turns left", 0 < (yaw1 - player(e, 0, YAW)) % 256 < 128)
    e.hold("W")
    e.run(20)
    e.release()
    check("W opens the throttle", player(e, 0, SPEED) > spd0, f"{spd0} -> {player(e, 0, SPEED)}")
    e.hold("SPACE")
    e.run(12)
    check("SPACE fires", player(e, 0, COOLDOWN) > 0)
    e.run(10)
    e.release()
    e.run(30)
    e.screenshot(SHOTS / "play.png")
    check("the view and the HUD drawn", len(e.screenshot().getcolors(1 << 16) or []) >= 4)
    # game over: no lives left, the player shot down (the rules end the game)
    e.write_mem(f"_world+{LIVES}", bytes([0, 0]))
    e.write_mem(f"_world+{ALIVE}", bytes([0, 0]))
    check("GameWorld.state found (playing)", int.from_bytes(e.read_mem(f"_world+{STATE}", 2), "little") == 1)
    so = STATE
    e.write_mem(f"_world+{so}", bytes([2, 0, 1, 0]))             # STATE_DYING, timer 1
    check("game over", e.run_until("ace: state=3", 50))
    e.run(60)
    e.screenshot(SHOTS / "gameover.png")
    e.run(400)                          # the original's 200 ticks of GAME OVER
    e.press("SPACE", hold_frames=12)
    check("FIRE after game over: the title", e.run_until("ace: title", 50))

    if failures:
        print("\n".join(e.debug_log[-15:]))

# two players: the split view, both flown by the autopilot, from a fresh boot
# (the rules' random numbers run on from game to game, as the original's)
with Emulator(machine="next") as e:
    e.load(NEX)
    e.run_until("ace: ready", 2000)
    e.run(10)
    e.press("2")
    check("2: two players", e.run_until("ace: players=2", 20))
    e.press("B")
    check("2 then B: a two-player autopilot game", e.run_until("ace: start players=2 auto=1", 50))
    e.run_until("ace: hash t=1000", 2000)
    e.run(2)
    e.screenshot(SHOTS / "two_players.png")
    check("two players: 3000 ticks", e.run_until("ace: hash t=3000", 5000))
    e.run(1)
    got = [h for h in next_hashes(e)[-3:]]
    want = host_hashes(2, 3000)
    check("two players: the Z80's game = the host's", got == want, f"next {got} host {want}")
    fps2 = fps_values(e)[-6:]
    if fps2:
        print(f"  frame rate, two players: {min(fps2)}-{max(fps2)} fps")
    if failures:
        print("\n".join(e.debug_log[-15:]))

print(f"{'FAIL' if failures else 'PASS'}: ace_pilot")
sys.exit(1 if failures else 0)
