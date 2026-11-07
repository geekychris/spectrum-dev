"""Emberkeep (Next) - plays the game through the harness.

1. The title; SPACE starts. The bot (tools/bot.py, which plans with the
   game's own rules built for the host) clears chambers 1-3, collecting the
   runes and oil, then walks into the spikes on purpose (bot.suicide): three
   lives lost, GAME OVER, initials, the title with the new score. The session
   is recorded and replayed twice (every frame hash must match).
2. A second game played to the end: all four chambers, THE DARK IS BEATEN.
   The frame rate is measured over it, and screenshots/GIF are taken.

While the bot plays, every game loop is checked against the host build of
the rules (the same C run for the same frames with the same keys): the
Next's game state must match it byte for byte.
"""
import ctypes
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / "tools" / "zxdev" / "src"))
sys.path.insert(0, str(HERE / "tools"))

from zxdev.emu import Emulator  # noqa: E402
from bot import Bot, State, GM_PLAYING, GM_WIN, GM_LOSE  # noqa: E402

NEX = HERE / "build" / "emberkeep.nex"
SHOTS = HERE / "screenshots"
REC = HERE / "build" / "bot-game.json"
GIF = ROOT / "docs" / "images" / "emberkeep.gif"
failures = []
bot = Bot()
compared, mismatches = [0], []


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


def same_as_host(prev, gs, keys):
    """The frames between two looks, run on the host from the first look with
    the same keys, must give the second look's state."""
    frames = (struct.unpack_from("<H", gs, 424)[0] - struct.unpack_from("<H", prev, 424)[0]) & 0xFFFF
    bot.put(prev)
    for _ in range(frames):
        bot.lib.sim_tick(keys)
    host = ctypes.string_at(bot.lib.sim_gs(), bot.size)
    compared[0] += frames
    if host != gs:
        mismatches.append((struct.unpack_from("<H", gs, 424)[0], [i for i in range(bot.size) if host[i] != gs[i]][:8]))


def drive(e, until, max_loops=40000, each=None):
    """The bot plays: the emulator stops at each game loop's frame_wait, the
    bot reads gs and holds its keys. Returns the log."""
    bp = e.break_at("frame_wait")
    held, log, prev = "", "", None
    for _ in range(max_loops):
        e.continue_(50)
        gs = e.read_mem("gs", bot.size)
        s = State(gs)
        if prev and prev[0][0] == GM_PLAYING and s.mode == GM_PLAYING and State(prev[0]).level == s.level \
                and State(prev[0]).lives == s.lives:
            same_as_host(prev[0], gs, prev[1])
        bits = bot.decide(gs)
        prev = (gs, bits)
        keys = Bot.key_names(bits)
        if keys != held:
            e.release()
            if keys:
                e.hold(keys)
            held = keys
        log += e.log()
        if each:
            each(e, s, log)
        if until(log, s):
            break
    e.release()
    e.delete(bp["index"])
    return log


def boot(e):
    e.load(NEX)
    ok = e.run_until("ember: ready", 3000)
    e.run(30)
    return ok


# ------------------------------------------------------------ 1. a game lost
with Emulator() as e:
    check("boots to the title", boot(e))
    e.record_input("start", from_reset=True)
    e.run_until("ember: ready", 3000)
    e.run(60)
    e.screenshot(SHOTS / "title.png")
    e.press("SPACE")
    check("SPACE starts chamber 1", e.run_until("mode=playing", 100))
    log = drive(e, lambda log, s: s.level >= 3 and s.mode == GM_PLAYING)
    scores = [int(v) for v in re.findall(r"ember: score=(\d+)", log)]
    check("runes taken (100 each) and three chambers cleared", "ember: level=3" in log and len(scores) > 6,
          log[-300:])
    check("a chamber scores 500 + half the lantern", any(b - a > 500 for a, b in zip(scores, scores[1:])), str(scores))
    bot.suicide = True                          # into the spikes
    log = drive(e, lambda log, s: s.mode == GM_LOSE)
    deaths = re.findall(r"ember: death", log)
    check("lives lost on the spikes", len(deaths) == 3, log[-300:])
    check("game over", "mode=lose" in log)
    e.run(60)
    e.screenshot(SHOTS / "gameover.png")
    e.run(120)                                  # the original's 150-frame pause
    e.press("SPACE")
    check("the high score asks for initials", e.run_until("hiscore rank=", 200))
    e.run(10)
    e.screenshot(SHOTS / "initials.png")
    e.type("BOT")
    check("initials entered", e.run_until("ember: name=BOT", 200))
    check("back to the title", e.run_until("ember: title", 200))
    e.run(40)
    e.screenshot(SHOTS / "hiscores.png")
    rec = e.record_input("save", REC)
    print(f"  recorded {rec['frames']} frames, {rec['events']} input changes")
check("the Next plays as the host build of the rules, frame by frame", compared[0] > 1000 and not mismatches,
      f"{compared[0]} frames, mismatches at {mismatches[:3]}")

seqs = []
for i in range(2):
    with Emulator() as e:
        r = e.replay(REC)
        seqs.append([h for _, h in r["hashes"]])
        check(f"replay {i + 1}: every frame hash matches the recorded game", r["ok"],
              f"{r.get('mismatches')} mismatches, first at {r.get('first_mismatch')}")
check("two replays give identical screens", seqs[0] == seqs[1] and len(seqs[0]) > 500)

# ------------------------------------------------------------ 2. a game won, frame rate, pictures
bot = Bot()
compared[0] = 0
with Emulator() as e:
    boot(e)
    e.press("SPACE")
    e.run_until("mode=playing", 100)
    perf0 = struct.unpack("<3H", e.read_mem("ember_perf", 6))
    shots, frames = set(), []

    def pictures(e, s, log):
        near = [en for en in s.enemies if abs(en[0] - s.x) < 34 and abs(en[1] - s.y) < 34]
        if s.mode == GM_PLAYING and near and "play" not in shots:     # a guard in the light
            e.screenshot(SHOTS / "play.png")
            shots.add("play")
        if s.level == 3 and s.mode == GM_PLAYING and len(frames) < 150:
            frames.append([e.screenshot(), s.tick])

    log = drive(e, lambda log, s: s.mode == GM_WIN, each=pictures)
    perf = struct.unpack("<3H", e.read_mem("ember_perf", 6))
    ticks, draws = perf[1] - perf0[1], perf[2] - perf0[2]
    fps = 50.0 * draws / max(1, ticks)
    print(f"  the whole game: {ticks} frames of play, {draws} screens: {fps:.1f} fps")
    check("all four chambers: THE DARK IS BEATEN", "mode=win" in log, log[-300:])
    check("50 fps", fps >= 49, f"{fps:.1f}")
    check("a guard seen in the lantern light", "play" in shots)
    e.run(40)
    e.screenshot(SHOTS / "won.png")
check("still the host's rules, frame by frame", compared[0] > 1000 and not mismatches, str(mismatches[:3]))

if frames:
    from PIL import Image
    imgs = [f[0].resize((f[0].width // 2, f[0].height // 2), Image.NEAREST).convert("P", palette=Image.ADAPTIVE, colors=64) for f in frames]
    durs = [20 * max(1, (b[1] - a[1]) & 0xFFFF) for a, b in zip(frames, frames[1:])] + [1000]
    GIF.parent.mkdir(parents=True, exist_ok=True)
    imgs[0].save(GIF, save_all=True, append_images=imgs[1:], duration=durs, loop=0, optimize=True)

print(f"{'FAIL' if failures else 'PASS'}: emberkeep")
sys.exit(1 if failures else 0)
