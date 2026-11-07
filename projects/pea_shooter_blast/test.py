"""Pea Shooter Blast (Next) - the harness test.

  - boots at 28 MHz to the title, the original's MOD playing on TurboSound;
    the high-score page
  - the Next computes what the original computes: a scripted 2,500-frame
    game on the original's rules gives the same state hash as
    tools/host_trace.sh (the original's code compiled for the host)
  - the Kempston joystick starts a game and drives the tank
  - a recorded game: the bot plays level 1 with the keys (P and SPACE
    held: right and shooting; Q to jump where the shortest way through that
    tools/reach.c finds jumps); it shoots enemies, scores and reaches the exit gate (LEVEL
    CLEAR); in level 2 it drives into the spike pit until every life is
    gone: game over, the initials, back to the title with them in the table
  - the scroll draws only the new columns each frame; frame rate
  - the recording replays twice with every frame's hash the same
  - screenshots in screenshots/, a GIF in docs/images

    ./zx test pea_shooter_blast
"""
import re
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "zxdev" / "src"))

from zxdev.emu import Emulator  # noqa: E402

HERE = Path(__file__).resolve().parent
NEX = HERE / "build" / "pea_shooter_blast.nex"
SHOTS = HERE / "screenshots"
GIF = ROOT / "docs" / "images" / "pea_shooter_blast.gif"
REC = HERE / "build" / "bot-game.json"
MODE_TITLE, MODE_GAME, MODE_ENTRY = range(3)
UI_PAGE = 84                                    # src/memmap.h: probe.c's page
ST_TITLE, ST_PLAYING, ST_DEAD, ST_GAMEOVER, ST_LEVELCLEAR = range(5)
failures = []


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


PROBE = struct.Struct("<10B3hl3H")
FIELDS = "mode state level lives health weapon alive on_ground facing fps tx ty scroll score invuln overruns cols".split()


def probe(e):
    """probe.c's struct: in page 84 (memmap.h), read through the page, as
    0x6000 may hold a Layer 2 page when the frame ends"""
    a = e.addr("probe")
    return dict(zip(FIELDS, PROBE.unpack(e.read_mem(a & 0x1FFF, PROBE.size, page=UI_PAGE))))


def host(cmd):
    return subprocess.run(cmd, cwd=HERE, capture_output=True, text=True, check=True).stdout


def jumps_of(plan_lines):
    """tools/reach.c's path -> its jumps: (x where the tank starts the jump,
    frames the jump is held); the rest of the way is right"""
    out, prev = [], None
    for line in plan_lines:
        act, x, _ = line.split()
        if act == "J":
            if prev == "J":
                out[-1][1] += 8
            else:
                out.append([int(x), 8])
        else:
            assert act == "R", f"the level 1 path only goes right and jumps ({line})"
        prev = act
    return out


def play_route(e, jumps, max_frames=6000, shots=None):
    """the bot: right and fire held all the time (P + SPACE), and each of
    the path's jumps (Q) when the tank gets to its x on the ground (so it
    doesn't depend on frame timing); after a death, from the start again.
    shots: collects a screenshot every other frame (up to 160)"""
    mark, frames, k, jump_left = len(e.log(0)), 0, 0, 0
    held = None
    while frames < max_frames:
        p = probe(e)
        if p["state"] == ST_LEVELCLEAR or p["mode"] != MODE_GAME:
            break
        if p["state"] == ST_DEAD:
            k, jump_left = 0, 0
        while k < len(jumps) and p["tx"] > jumps[k][0] + 12:          # passed it (a respawn: from the start)
            k += 1
        if not jump_left and k < len(jumps) and p["on_ground"] and p["tx"] >= jumps[k][0] - 4:
            jump_left = max(jumps[k][1], 24)        # a full jump (the bot reads the tank every 2 frames)
            k += 1
        keys = "P+SPACE" + ("+Q" if jump_left else "")
        if p["state"] == ST_DEAD:
            keys = ""
        if keys != held:
            e.release()
            if keys:
                e.hold(keys)
            held = keys
        e.run(2)
        if shots is not None and len(shots) < 160 and p["state"] == ST_PLAYING:
            shots.append(e.screenshot().convert("RGB"))
        frames += 2
        jump_left = max(0, jump_left - 2)
    e.release()
    return e.log(0)[mark:]


SHOTS.mkdir(exist_ok=True)
want_hash = host(["sh", "tools/host_trace.sh"]).split()[0]
host(["sh", "tools/reach.sh"])
jumps1 = jumps_of(host(["build/host/reach", "--plan", "1"]).strip().splitlines())

with Emulator(machine="next") as e:
    e.load(NEX)
    check("boots to the title", e.run_until("psb: ready", 300))
    turbo = e.raw("get-cpu-turbo-speed").strip()
    check("runs at 28 MHz", turbo.startswith("8"), turbo)
    e.run(100)
    e.screenshot(SHOTS / "title.png")
    mark = len(e.log(0))
    e.run(200)
    m = re.findall(r"ay=(\d+),(\d+),(\d+)", e.log(0)[mark:])
    a = [int(v) for v in m[-1]] if m else None
    check("the original's MOD plays on TurboSound (chip 0 tones, chip 1 the echo channel)",
          a is not None and (a[0] & 7) != 7 and a[1] + a[2] > 0, str(a))
    e.run(60)
    e.screenshot(SHOTS / "hiscores.png")

    # the original's rules on the Next = on the host
    e.poke(e.addr("hash_req"), 1)
    ok = e.run_until("psb: HASH", 20000)
    e.run(2)
    got = re.findall(r"psb: HASH (\w+)", e.log(0))
    check("a scripted game: the same state hash as the original's code on the host",
          ok and got and got[-1] == want_hash, f"{got[-1:]} != {want_hash}")
    e.run(30)

    # Kempston: fire starts, right moves the tank
    e.press("FIRE")
    e.run_until("psb: STATE PLAYING", 200)
    e.run(4)
    p0 = probe(e)
    check("Kempston fire starts a game", p0["mode"] == MODE_GAME and p0["state"] == ST_PLAYING, str(p0))
    e.hold("RIGHT")
    e.run(40)
    e.release()
    p1 = probe(e)
    check("Kempston right drives the tank", p1["tx"] > p0["tx"] + 40, f"{p0['tx']} -> {p1['tx']}")
    e.press("CAPS+SPACE")
    e.run(10)
    check("BREAK goes back to the title", probe(e)["mode"] == MODE_TITLE)

    # ---- a recorded game ----
    e.record_input("start", from_reset=True)
    check("boots again (recording)", e.run_until("psb: ready", 300))
    e.run(30)
    e.press("SPACE")
    e.run_until("psb: STATE PLAYING", 200)      # game_init and the first full screen take a few frames
    e.run(4)
    p = probe(e)
    check("SPACE starts level 1", p["mode"] == MODE_GAME and p["level"] == 0 and p["lives"] == 3, str(p))
    gif = [] if ("--gif" in sys.argv or not GIF.exists()) else None
    log = play_route(e, jumps1, max_frames=400, shots=gif)
    e.screenshot(SHOTS / "play.png")
    log += play_route(e, jumps1, shots=gif)
    if gif:
        GIF.parent.mkdir(parents=True, exist_ok=True)
        small = [im.resize((im.width // 2, im.height // 2)) for im in gif[40:]]
        small[0].save(GIF, save_all=True, append_images=small[1:], duration=40, loop=0, optimize=True)
        print(f"  wrote {GIF.relative_to(ROOT)} ({len(small)} frames)")
    p = probe(e)
    check("the bot shoots enemies and scores", p["score"] >= 300, str(p["score"]))
    fps = [int(v) for v in re.findall(r"fps=(\d+) mode=1", log)]
    e.run(20)                                   # play_route stops on LEVEL CLEAR
    e.screenshot(SHOTS / "levelclear.png")
    log += e.log(0)[-400:]
    check("the bot reaches level 1's gate: LEVEL CLEAR", "STATE LEVELCLEAR level=0" in log, log[-400:])
    check("level 2 comes", e.run_until("STATE PLAYING level=1", 300))
    e.run(60)
    e.screenshot(SHOTS / "level2.png")
    # into level 2's spike pit until the game is over
    lives0 = probe(e)["lives"]
    e.hold("P")
    mark = len(e.log(0))
    ok = e.run_until("STATE GAMEOVER", 20000)
    e.release()
    log2 = e.log(0)[mark:]
    deaths = re.findall(r"STATE DEAD level=1 lives=(\d)", log2)
    check("the spikes take every life", ok and len(deaths) == lives0, f"{deaths} from {lives0}")
    e.run(30)
    e.screenshot(SHOTS / "gameover.png")
    check("then the high-score entry", e.run_until("psb: HISCORE rank=", 400))
    e.run(10)
    for ch in "ZXN":
        if ch == "N":
            e.screenshot(SHOTS / "entry.png")
        e.press(ch)
        e.run(4)
    check("initials typed: back to the title", e.run_until("psb: TITLE", 100) and "psb: NAME ZXN" in e.log(0))
    hs = e.read_mem("hs_table", 40)
    names = [hs[i * 8:i * 8 + 3].decode() for i in range(5)]
    check("ZXN is in the high-score table", "ZXN" in names, str(names))
    e.run(40)
    e.screenshot(SHOTS / "table.png")
    over = probe(e)["overruns"]
    rec = e.record_input("save", REC)
    print(f"  recorded {rec['frames']} frames, {rec['events']} input changes")
    if fps:
        print(f"  frames drawn a second while playing: min {min(fps)}, mean {sum(fps) / len(fps):.1f} "
              f"({len(fps)} samples of 5 s); {over} late frames in the game")
    check("plays at 50 frames a second", fps and min(fps) >= 45, str(fps))

seqs = []
for i in range(2):
    with Emulator(machine="next") as e:
        r = e.replay(REC)
        seqs.append([h for _, h in r["hashes"]])
        check(f"replay {i + 1}: every frame hash matches the recorded game", r["ok"],
              f"{r.get('mismatches')} mismatches, first at {r.get('first_mismatch')}")
check("two replays give identical screens", seqs[0] == seqs[1] and len(seqs[0]) > 1000)

print(f"{'FAIL' if failures else 'PASS'}: pea_shooter_blast")
sys.exit(1 if failures else 0)
