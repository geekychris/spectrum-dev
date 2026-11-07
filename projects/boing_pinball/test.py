"""Boing Pinball (Next) - plays a whole game through the harness.

A bot reads the balls (`pin_probe`, by symbol) and plays with the keys the
way the original's demo player (pb_bot) does: it pulls the plunger back and
lets go, and flips when a ball comes down onto a flipper. Balls drain in
the end (three balls a game), so: start, score, lose balls, game over,
high-score entry, the title. The game is recorded and replayed twice
(every frame hash must match). Screenshots go to screenshots/.
"""
import math
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "zxdev" / "src"))

from zxdev.emu import Emulator  # noqa: E402

HERE = Path(__file__).resolve().parent
NEX = HERE / "build" / "boing_pinball.nex"
SHOTS = HERE / "screenshots"
REC = HERE / "build" / "bot-game.json"
# the flippers (pb_tables.c): pivot mm, length mm, rest angle; key
FLIPPERS = [((150, 126), 76, -0.52359, "Z"), ((328, 126), 76, 3.665176, "M")]
BS_PLAY, BS_LANE = 0, 1
failures = []
DEBUG = "--debug" in sys.argv

# ---- the palettes in the hardware against the generated tables (src/gfx_0.c) ----
def _c_array(name):
    src = (HERE / "src" / "gfx_0.c").read_text()
    body = src[src.index(name):]
    body = body[body.index("{") + 1:body.index("}")]
    return [int(x) for x in body.replace("\n", " ").split(",") if x.strip()]


L2_PAL, SPR_PAL = _c_array("l2_pal_0[256]"), _c_array("spr_pal[16]")
LAMP_ON, LAMP_OFF, LAMP_USED = _c_array("lamp_on_0["), _c_array("lamp_off_0["), _c_array("lamp_used_0[")
P_RED, P_FLASH, P_RUBBER, P_METAL_L, P_METAL_D, P_ORANGE, P_BLACK = 12, 14, 10, 7, 6, 24, 0   # gfx.h


def palette_problems(e):
    """entries that are wrong: static ones must be the table's, animated ones one of their colours"""
    second = bool(e._reg(0x43) & 4)                       # the display swaps Layer 2 palettes
    hw, hs = e._palette("layer2", second), e._palette("sprite", False)
    allowed = {i: {L2_PAL[i]} for i in range(256)}
    for i in range(29):
        if LAMP_USED[i]:
            allowed[32 + i] = {LAMP_ON[i], LAMP_OFF[i]}
    for i in range(8):
        allowed[64 + i] = {L2_PAL[P_ORANGE], L2_PAL[P_BLACK]}
    for i in range(10):
        allowed[72 + i] = {L2_PAL[P_RED], L2_PAL[P_FLASH]}
    for i in range(2):
        allowed[82 + i] = {L2_PAL[P_RUBBER], L2_PAL[P_FLASH]}
        allowed[84 + i] = {L2_PAL[P_METAL_L], L2_PAL[P_METAL_D]}
    for i in range(88, 91):                               # the display's pens: lit, dim or background
        allowed[i] = {L2_PAL[27], L2_PAL[28], L2_PAL[26]}
    bad = [(i, hw[i]) for i in range(256) if hw[i] not in allowed[i]]
    bad += [("sprite", i, hs[i]) for i in range(16) if hs[i] != SPR_PAL[i]]
    return bad


def lamps_lit(e):
    second = bool(e._reg(0x43) & 4)
    hw = e._palette("layer2", second)
    return [i for i in range(29) if LAMP_USED[i] and LAMP_ON[i] != LAMP_OFF[i] and hw[32 + i] == LAMP_ON[i]]


# ---- the dot-matrix display: every frame must show one whole text ----
import re as _re
_FONT = [[int(v, 16) for v in r.split(", ")] for r in
         _re.findall(r"\{ (0x[0-9A-F]{2}(?:, 0x[0-9A-F]{2}){6}) \}", (ROOT / "sdk" / "src" / "font5.c").read_text())]
DMD_X, DMD_Y, DMD_W, DMD_H = 168, 58, 144, 48          # src/gfx.h


def dmd_expected(big, small):
    """the lit pixels for a text, laid out as front.c's dmd() does"""
    w = lambda t, sx: len(t) * 6 * sx - sx if t else 0
    lines = []
    if not small:
        sx = 3 if w(big, 3) <= DMD_W else 2 if w(big, 2) <= DMD_W else 1
        lines.append((big, sx, (DMD_H - 7 * (sx + 1)) // 2, sx + 1))
    else:
        sx = 2 if w(big, 2) <= DMD_W else 1
        lines += [(big, sx, 5, 3), (small, 1, 31, 2)]
    lit = set()
    for t, sx, y0, sy in lines:
        x0 = int((DMD_W - w(t, sx)) / 2)                  # C division (towards 0)
        for k, ch in enumerate(t):
            o = ord(ch) - 33
            if not 0 <= o < len(_FONT):
                continue
            for c in range(5):
                for r in range(7):
                    if _FONT[o][r] & (0x80 >> c):
                        for i in range(sx):
                            x = x0 + (k * 6 + c) * sx + i
                            if 0 <= x < DMD_W:
                                for j in range(sy - 1):
                                    lit.add((x, y0 + r * sy + j))
    return lit


def dmd_check_frames(e, frames):
    """screenshots of `frames` frames: the display shows exactly the text the
    game says is visible (dmd_vis_*), never part of one"""
    bad = []
    for f in range(frames):
        e.run(1)
        big = e.read_mem("dmd_vis_big", 24).split(b"\0")[0].decode()
        small = e.read_mem("dmd_vis_small", 40).split(b"\0")[0].decode()
        img = e.screenshot()
        sc = img.width // 352                            # 320 + a 16-pixel border each side
        P = lambda x, y: img.getpixel(((16 + DMD_X + x) * sc, (24 + DMD_Y + y) * sc))[:3]
        bg = P(DMD_W - 1, DMD_H - 1) if not big else None
        want = dmd_expected(big, small)
        if bg is None:                                   # the background: a pixel no text ever lights
            bg = P(0, 0)
        got = {(x, y) for x in range(DMD_W) for y in range(DMD_H) if P(x, y) != bg}
        if got != want:
            bad.append((f, big, small, len(got ^ want)))
    return bad



def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


def probe(e):
    b = e.read_mem("pin_probe", 38)
    mode, n = struct.unpack("<bb", b[:2])
    v = struct.unpack("<16h", b[2:34])
    st = struct.unpack("<4b", b[34:38])
    balls = [dict(x=v[i] / 16, y=v[4 + i] / 16, vx=v[8 + i] / 4, vy=v[12 + i] / 4, state=st[i]) for i in range(max(0, min(n, 4)))]
    return mode, balls


def bot_game(e, frames=30000):
    """the original's pb_bot, with keys: returns the log"""
    log = ""
    hold = {"Z": 0, "M": 0}
    pull = 0
    t = 0
    shot = False
    while t < frames:
        mode, balls = probe(e)
        if mode != 1:                                      # not playing any more
            break
        keys = []
        lane = any(b["state"] == BS_LANE for b in balls)
        if lane:
            pull += 1
            if pull < 40:
                keys.append("SPACE")                       # pull back ~0.8 s, then let go
            elif pull > 44:
                pull = 0
        for b in balls:
            if b["state"] != BS_PLAY:
                continue
            for (px, py), ln, rest, key in FLIPPERS:
                tx, ty = px + ln * math.cos(rest), py + ln * math.sin(rest)
                dx, dy = b["x"] - (px + tx) / 2, b["y"] - (py + ty) / 2
                if b["vy"] < -60 and abs(dx) < ln * 3 / 4 + 13.5 and 0 < dy < 40 + abs(b["vy"]) / 20 * 2.4:
                    hold[key] = 12                         # 0.25 s
        for k in hold:
            if hold[k] > 0:
                keys.append(k)
                hold[k] -= 1
        if keys:
            e.hold(" ".join(keys))
        else:
            e.release()
        e.run(1)
        t += 1
        log += e.log()
        if not shot and t > 700 and any(b["y"] > 500 for b in balls):
            e.screenshot(SHOTS / "play.png")
            shot = True
    e.release()
    return log


with Emulator(machine="next") as e:
    e.load(NEX)
    check("boots to the title (the attract demo)", e.run_until("pinball: ready", 3000))
    check("TurboSound AY found", e.read_mem("ay_present", 1)[0] == 1)
    bad = palette_problems(e)
    check("Layer 2 and sprite palettes in the hardware are the generated ones", not bad, str(bad[:8]))
    e.run(200)
    e.screenshot(SHOTS / "title.png")
    # the attract mode's display changes every 3 s: every frame across three changes
    bad = dmd_check_frames(e, 480)
    check("the display is never seen half drawn (attract, 480 frames)", not bad, str(bad[:3]))
    e.record_input("start", from_reset=True)
    e.run_until("pinball: ready", 3000)
    e.run(100)
    e.press("SPACE", hold_frames=2, release_frames=2)
    check("FIRE starts a game", e.run_until("pinball: start", 50))
    lit = set()
    for _ in range(40):                                   # the skill-shot lane blinks while the ball waits
        e.run(2)
        lit |= set(lamps_lit(e))
    check("a lamp lights up (its palette entry gets its 'on' colour)", bool(lit), str(lit))
    bad = palette_problems(e)
    check("palettes still right in a game", not bad, str(bad[:8]))
    bad = dmd_check_frames(e, 150)                      # the game's first messages, the score
    check("the display is never seen half drawn (a game's start, 150 frames)", not bad, str(bad[:3]))
    log = bot_game(e)
    scores = [int(s) for s in re.findall(r"pinball: score=(\d+)", log)]
    check("the bot scores points", scores and max(scores) >= 5000, str(scores[-3:]))
    check("bumpers, targets or ramps hit", any(k in log for k in ("pinball: bumper", "pinball: drop", "pinball: ramp")))
    drains = log.count("pinball: drain")
    check("balls drain (lives lost)", drains >= 3, str(drains))
    check("three balls, then game over", "pinball: gameover" in log, log[-300:])
    m = re.search(r"gameover score=(\d+) overruns=(\d+)", log)
    final = int(m.group(1)) if m else 0
    e.run(30)
    e.screenshot(SHOTS / "gameover.png")
    e.run(150)
    e.press("SPACE", hold_frames=2, release_frames=2)
    more = ""
    for _ in range(60):
        e.run(5)
        more += e.log()
        if "hiscore rank" in more or "pinball: title" in more:
            break
    if "hiscore rank" in more:
        e.run(2)                                           # typing at once: the letters are latched
        e.type("BOT")
        e.run(10)
        more += e.log()
        check("high-score name entry", "pinball: name=BOT" in more, more[-200:])
        e.run_until("pinball: title", 300)
        more += "pinball: title"
    else:
        check(f"score {final} below the table: no entry", final <= 20000)
    check("back to the title", "pinball: title" in more)
    e.run(150 * 3)                                         # the display shows the scores in turn
    e.screenshot(SHOTS / "hiscores.png")
    rec = e.record_input("save", REC)
    print(f"  final score {final}, {drains} drains; recorded {rec['frames']} frames, {rec['events']} input changes")

seqs = []
for i in range(2):
    with Emulator(machine="next") as e:
        r = e.replay(REC)
        seqs.append([h for _, h in r["hashes"]])
        check(f"replay {i + 1}: every frame hash matches the recorded game", r["ok"],
              f"{r.get('mismatches')} mismatches, first at {r.get('first_mismatch')}")
check("two replays give identical screens", seqs[0] == seqs[1] and len(seqs[0]) > 1000)

print(f"{'FAIL' if failures else 'PASS'}: boing_pinball")
sys.exit(1 if failures else 0)
