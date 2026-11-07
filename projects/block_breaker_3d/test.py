"""Block Breaker 3D (Next) - the harness test.

  - boots at 28 MHz to the title, with the title music on two TurboSound chips
  - the original's own tests (tests/test_cases.c, 42 of them, including its
    20,000-tick autopilot game whose state hash must be the original's) run
    inside the game
  - the help pages, and Kempston up/down choosing the start level
  - a recorded game: a bot (the original's autopilot, bb_bot, in Python,
    steering with Q A O P and SPACE, then with the Kempston joystick) breaks
    bricks and clears level 1; the bricks in Layer 2 match a host render of
    the same cells (render.c's faces and draw order); then it misses on
    purpose until the game is over, types its initials into the high-score
    table and the game goes back to the title
  - the recording replays twice with every frame's hash matching
  - frame rate while playing; screenshots in screenshots/, a GIF in docs/images

    ./zx test block_breaker_3d
"""
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "zxdev" / "src"))
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE / "tools"))

from zxdev.emu import Emulator  # noqa: E402

NEX = HERE / "build" / "block_breaker_3d.nex"
SHOTS = HERE / "screenshots"
GIF = ROOT / "docs" / "images" / "block_breaker_3d.gif"
REC = HERE / "build" / "bot-game.json"
FX = 256
BB_W, BB_H, BB_D, BB_R = 168, 120, 800, 10
BS_TITLE, BS_PLAY, BS_OVER, BS_ENTRY = range(4)
failures = []


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


PROBE = struct.Struct("<8B5hl4B4H4B3h2B")
FIELDS = ("state level lives stuck between over won autoplay bx by bz px py score left fps help paused "
          "broken paddle_hits overruns lines caps parts wide plough vx vy vz start_level entry_pos").split()


def probe(e):
    return dict(zip(FIELDS, PROBE.unpack(e.read_mem("probe", PROBE.size))))


# ---- the bot: the original's bb_bot (bb_core.c), steering with keys ----

def fold(p, lim):
    period = 4 * lim
    q = (p + lim) % period
    if q > 2 * lim:
        q = period - q
    return q - lim


class Bot:
    """bb_bot's aim: where the ball comes back to the paddle's plane, taken
    off-centre by a varying amount so it goes everywhere; miss=True aims at
    the opposite corner instead"""

    def __init__(self):
        self.n = 0
        self.miss = False
        self.joy = False

    def keys(self, p):
        pw, ph, dead = (62 if p["wide"] else 40), (42 if p["wide"] else 28), 6
        if p["stuck"]:
            return ["SPACE" if not self.joy else "FIRE"] if self.n % 4 < 2 else []
        bx, by, bz = p["bx"], p["by"], p["bz"]
        vx, vy, vz = p["vx"] / FX, p["vy"] / FX, p["vz"] / FX
        if vz < 0:
            t = (bz - BB_R) / -vz
        elif vz > 0:
            t = ((BB_D - BB_R) - bz + (BB_D - 2 * BB_R)) / vz
        else:
            t = 0
        tx, ty = fold(bx + vx * t, BB_W - BB_R), fold(by + vy * t, BB_H - BB_R)
        h = (p["paddle_hits"] * 2654435761 + 12345) & 0xFFFFFFFF
        tx -= ((h % 121) - 60) * pw / 100
        ty -= (((h >> 10) % 121) - 60) * ph / 100
        if self.miss:
            tx, ty = (-120 if tx > 0 else 120), (-80 if ty > 0 else 80)
        out = []
        if tx > p["px"] + dead:
            out.append("RIGHT" if self.joy else "P")
        if tx < p["px"] - dead:
            out.append("LEFT" if self.joy else "O")
        if ty > p["py"] + dead:
            out.append("UP" if self.joy else "Q")
        if ty < p["py"] - dead:
            out.append("DOWN" if self.joy else "A")
        return out


def steer(e, bot, until, max_frames, every=2):
    """bot steers until until(probe, log) or max_frames; returns the log"""
    held = None
    mark = len(e.log(0))
    for f in range(0, max_frames, every):
        p = probe(e)
        k = bot.keys(p)
        bot.n += 1
        if k != held:
            e.release()
            if k:
                e.hold("+".join(k))
            held = k
        e.run(every)
        if until(p, e.log(0)[mark:]):
            break
    e.release()
    return e.log(0)[mark:]


# ---- the bricks in Layer 2, rendered on the host (render.c / l2wpoly.c) ----

def host_layer2(cells, hits):
    import gen_gfx as G
    l2 = bytearray(b"".join((HERE / "gfx" / f"tunnel_{k}.bin").read_bytes() for k in range(5)))
    sx = [[G.proj_x(x, z) for x in G.BX] for z in G.BZ]
    sy = [[G.proj_y(y, z) for y in G.BY] for z in G.BZ]

    def pen(t, h):
        if 1 <= t <= 6:
            return 11 + 2 * (t - 1)
        return (23 if h > 1 else 24) if t == 7 else 25 if t == 8 else 29

    def shade(t, h):
        if t == 7 and h <= 1:
            return 3
        return 22 if t == 9 else pen(t, h) + 1

    def tdiv(a, b):
        q = abs(a) // abs(b)
        return q if (a >= 0) == (b > 0) else -q

    def span(x, y0, y1, c):
        y0, y1 = max(y0, 0), min(y1, 256)
        if 0 <= x < 320 and y0 < y1:
            l2[x * 256 + y0:x * 256 + y1] = bytes([c]) * (y1 - y0)

    def rect(x0, y0, x1, y1, c):
        for x in range(max(x0, 0), min(x1, 320)):
            span(x, y0, y1, c)

    def poly(pts, c):
        xs = [p[0] for p in pts]
        lo, hi = max(min(xs), 0), min(max(xs), 320)
        if lo >= hi:
            return
        top, bot = {x: 32767 for x in range(lo, hi)}, {x: -32768 for x in range(lo, hi)}
        n = len(pts)
        for i in range(n):
            (xa, ya), (xb, yb) = pts[i], pts[(i + 1) % n]
            if xa == xb:
                continue
            if xa > xb:
                xa, ya, xb, yb = xb, yb, xa, ya
            dy = tdiv((yb - ya) << 16, xb - xa)
            s, en = max(xa, lo), min(xb, hi)
            y = (ya << 16) + dy * (s - xa) + 0x8000
            for x in range(s, en):
                v = y >> 16
                top[x], bot[x] = min(top[x], v), max(bot[x], v)
                y += dy
        for x in range(lo, hi):
            span(x, top[x], bot[x], c)

    def empty(c, r, l):
        return not (0 <= c < 6 and 0 <= r < 4 and 0 <= l < 3) or cells[(l * 4 + r) * 6 + c] == 0

    for l in (2, 1, 0):
        zf, zb, zface = 2 * l + 1, (6 if l == 2 else 2 * l + 3), 2 * l
        for pss in (0, 1):
            for i in range(l * 24, (l + 1) * 24):
                t = cells[i]
                if not t:
                    continue
                c, r = i % 6, (i // 6) % 4
                lit, sh = pen(t, hits[i]), shade(t, hits[i])
                if pss:
                    if empty(c, r, l - 1):
                        rect(sx[zf][c], sy[zf][r + 1], sx[zf][c + 1], sy[zf][r], sh)
                        rect(sx[zface][7 + c], sy[zface][9 + r], sx[zface][13 + c], sy[zface][5 + r], lit)
                    continue
                V = lambda xa, ya, yb: [(sx[zf][xa], sy[zf][ya]), (sx[zf][xa], sy[zf][yb]), (sx[zb][xa], sy[zb][yb]), (sx[zb][xa], sy[zb][ya])]
                H = lambda xa, xb, ya: [(sx[zf][xa], sy[zf][ya]), (sx[zf][xb], sy[zf][ya]), (sx[zb][xb], sy[zb][ya]), (sx[zb][xa], sy[zb][ya])]
                if c >= 4 and empty(c - 1, r, l):
                    poly(V(c, r, r + 1), sh)
                if c <= 1 and empty(c + 1, r, l):
                    poly(V(c + 1, r, r + 1), sh)
                if r == 0 and empty(c, r + 1, l):
                    poly(H(c, c + 1, r + 1), lit)
                if r == 3 and empty(c, r - 1, l):
                    poly(H(c, c + 1, r), sh)
    return bytes(l2)


def layer2_matches(e):
    """Layer 2 (banks 20-24) = the host's render of the cells now; tries for
    a few frames (one that is still drawing differs)"""
    bad = None
    for _ in range(12):
        g = e.read_mem("game", 152)
        cells, hits = g[8:80], g[80:152]
        got = b"".join(e.read_mem(0, 8192, page=40 + k) for k in range(10))
        want = host_layer2(cells, hits)
        bad = sum(1 for a, b in zip(got, want) if a != b)
        if bad == 0:
            return True, 0, sum(1 for c in cells if c)
        e.run(1)
    return False, bad, sum(1 for c in cells if c)


SHOTS.mkdir(exist_ok=True)
with Emulator(machine="next") as e:
    e.load(NEX)
    check("boots to the title", e.run_until("bb: ready", 300))
    turbo = e.raw("get-cpu-turbo-speed").strip()
    check("runs at 28 MHz", turbo.startswith("8"), turbo)
    e.run(120)
    e.screenshot(SHOTS / "title.png")
    p = probe(e)
    check("the title: the autopilot plays the demo (the ball served)", p["state"] == BS_TITLE and not p["stuck"] and p["bz"] > 30, str(p))
    mark = len(e.log(0))
    e.run(260)
    m = re.findall(r"ay=(\d+),(\d+),(\d+),(\d+)", e.log(0)[mark:])
    a = [int(v) for v in m[-1]] if m else None
    check("title music on two TurboSound chips (tones on, volumes up)",
          a is not None and (a[0] & 7) != 7 and (a[2] & 7) != 7 and a[1] + a[3] > 0, str(a))
    e.screenshot(SHOTS / "hiscores.png")

    # the original's tests, inside the game
    e.poke(e.addr("selftest_req"), 1)
    ok = e.run_until(" failed", 60000)
    st = re.findall(r"SELFTEST (\d+) passed, (\d+) failed", e.log(0))
    check("the original's 42 tests pass in the game (incl. its 20,000-tick hash)",
          ok and st and st[-1] == ("42", "0"), str(st[-1:]) + " " + str(re.findall(r"TEST FAIL .*", e.log(0))))

    # help pages
    e.press("H")
    e.run(10)
    check("H: the help, page 1", probe(e)["help"] == 1)
    e.screenshot(SHOTS / "help.png")
    e.press("H")
    e.run(10)
    check("H again: page 2", probe(e)["help"] == 2)
    e.press("H")
    e.run(10)
    check("and back to the title", probe(e)["help"] == 0)

    # Kempston on the title: up and down choose the start level
    e.press("UP")
    e.run(4)
    check("Kempston up: start at level 2", probe(e)["start_level"] == 2)
    e.press("DOWN")
    e.run(4)
    check("Kempston down: back to level 1", probe(e)["start_level"] == 1)

    # ---- a recorded game ----
    e.record_input("start", from_reset=True)
    check("boots again (recording)", e.run_until("bb: ready", 300))
    e.run(40)
    e.press("SPACE")
    e.run(4)
    p = probe(e)
    check("SPACE starts level 1 with the ball on the paddle", p["state"] == BS_PLAY and p["level"] == 1 and p["stuck"], str(p))
    e.run(20)
    e.screenshot(SHOTS / "serve.png")
    bot = Bot()
    log = steer(e, bot, lambda p, l: p["broken"] >= 6, 6000)
    p = probe(e)
    check("the bot (keys) serves, returns the ball and breaks bricks", p["broken"] >= 6 and p["score"] > 0, str(p))
    ok, bad, n = layer2_matches(e)
    check("Layer 2 = the host's render of the bricks (after breaks)", ok, f"{bad} pixels differ, {n} bricks")
    e.screenshot(SHOTS / "play.png")
    bot.joy = True
    b0 = p["broken"]
    log += steer(e, bot, lambda p, l: p["broken"] >= b0 + 6, 6000)
    p = probe(e)
    check("the bot (Kempston joystick) keeps breaking bricks", p["broken"] >= b0 + 6, str(p))
    bot.joy = False
    fps = []
    log += steer(e, bot, lambda p, l: "EV CLEAR a=1" in l, 40000)
    fps += [int(v) for v in re.findall(r"fps=(\d+) lines=\d+ state=1", log)]
    check("level 1 cleared", "EV CLEAR a=1" in log, log[-300:])
    e.run(6)
    e.screenshot(SHOTS / "cleared.png")
    check("the next level comes", e.run_until("EV LEVEL a=2", 200))
    e.run(6)
    ok, bad, n = layer2_matches(e)
    check("Layer 2 = the host's render of level 2's bricks", ok, f"{bad} pixels differ, {n} bricks")
    p = probe(e)
    lives0 = p["lives"]
    bot.miss = True
    log2 = steer(e, bot, lambda p, l: p["state"] != BS_PLAY, 20000)
    lost = re.findall(r"EV LOST a=(\d+)", log2)
    check("missing the ball costs the lives", len(lost) == lives0 and lost[-1] == "0", f"{lost} from {lives0}")
    check("game over", "EV OVER" in log2 and "STATE OVER" in log2, log2[-300:])
    e.run(10)
    e.screenshot(SHOTS / "gameover.png")
    check("then the high-score entry", e.run_until("bb: HISCORE rank=", 200))
    e.run(10)
    for ch in "ZXN":
        e.press(ch)
        e.run(4)
    e.screenshot(SHOTS / "entry.png")
    check("initials typed: back to the title", e.run_until("bb: STATE TITLE", 100) and "bb: NAME ZXN" in e.log(0))
    hs = e.read_mem("hs_table", 40)
    names = [hs[i * 8:i * 8 + 3].decode() for i in range(5)]
    check("ZXN is in the high-score table", "ZXN" in names, str(names))
    e.run(30)
    e.screenshot(SHOTS / "table.png")
    overruns = probe(e)["overruns"]
    rec = e.record_input("save", REC)
    print(f"  recorded {rec['frames']} frames, {rec['events']} input changes")
    if fps:
        print(f"  frames drawn a second while playing: min {min(fps)}, mean {sum(fps) / len(fps):.1f} "
              f"({len(fps)} samples of 5 s); {overruns} late frames in the game")
    check("plays at 45-50 frames a second", fps and min(fps) >= 40 and sum(fps) / len(fps) >= 48, str(fps))

seqs = []
for i in range(2):
    with Emulator(machine="next") as e:
        r = e.replay(REC)
        seqs.append([h for _, h in r["hashes"]])
        check(f"replay {i + 1}: every frame hash matches the recorded game", r["ok"],
              f"{r.get('mismatches')} mismatches, first at {r.get('first_mismatch')}")
check("two replays give identical screens", seqs[0] == seqs[1] and len(seqs[0]) > 1000)

if "--gif" in sys.argv or not GIF.exists():
    with Emulator(machine="next") as e:
        e.load(NEX)
        e.run_until("bb: ready", 300)
        e.run(30)
        e.press("SPACE")
        e.run(10)
        e.press("B")                                 # the autopilot
        e.run(1500)
        GIF.parent.mkdir(parents=True, exist_ok=True)
        e.record_video(GIF, frames=250, every=2)
        print(f"  wrote {GIF.relative_to(ROOT)}")

print(f"{'FAIL' if failures else 'PASS'}: block_breaker_3d")
sys.exit(1 if failures else 0)
