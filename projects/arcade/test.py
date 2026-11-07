"""Test for the arcade menu (run by `./zx test arcade` and ./zx selftest).

Builds the SD image (every game with an arcade.json, the menu, NextZXOS),
boots a copy of it on the Next and checks: NextZXOS runs autoexec.bas into
the menu; the list, the picture (bytes in Layer 2 = the file on the card,
sdfile's sd_load_l2w) and the details follow the cursor; S sorts by plays;
then every game is chosen from the menu and must reach its title (its
"ready" log line), and a reset brings the menu back with that game selected
and its play counted (stats.dat written with sd_save, read back with sd_load).

    .venv/bin/python projects/arcade/test.py [--gif] [--no-build]

--gif also records docs/images/arcade.gif.
"""
import re
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "zxdev" / "src"))

from zxdev import arcade  # noqa: E402
from zxdev.emu import Emulator  # noqa: E402

HERE = Path(__file__).resolve().parent
SHOTS = HERE / "screenshots"
failures = []


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


def last(pattern, text):
    m = re.findall(pattern, text)
    return m[-1] if m else None


def layer2_column(e, x, y, h):
    """h bytes of Layer 2 (320 x 256 mode, bank 9) column x from row y."""
    return e.read_mem((x % 32) * 256 + y, h, page=18 + x // 32)


def gif(e, gs, path):
    """The menu moving through the list and starting a game."""
    frames = []

    def film(n, every=2):
        for _ in range(0, n, every):
            e.run(every)
            frames.append(e.screenshot().convert("RGB"))

    film(20)
    for key in ["A"] * (len(gs) - 1) + ["Q"] * 2:
        e.hold(key)
        film(4)
        e.release()
        film(26)
    e.press("ENTER")
    film(120, 3)
    frames[0].save(path, save_all=True, append_images=frames[1:], loop=0, duration=60)


build = "--no-build" not in sys.argv
m = arcade.build(do_build=build, log=lambda s: None)
gs = m["games"]
check("games registered and on the card", len(gs) >= 2, str(len(gs)))
SHOTS.mkdir(exist_ok=True)

with tempfile.TemporaryDirectory() as tmp:
    sd = Path(tmp) / "arcade.mmc"
    arcade.fresh_copy(Path(m["image"]), sd)   # no plays yet; ZEsarUX writes stats.dat back into it
    with Emulator(machine="next", sd=sd) as e:
        sel = arcade.menu_ready(e, 3000)
        log = e.log_text(since=0)
        check("NextZXOS boots into the menu", sel is not None, log[-300:])
        check("the menu read games.dat", last(r"ready games=(\d+)", log) == str(len(gs)))
        check("A-Z order to start with, first game selected", sel == 0 and last(r"sort=(\d)", log) == "0")
        e.run(5)
        e.screenshot(SHOTS / "menu.png")

        # the picture in Layer 2 is the file on the card
        pic = arcade.picture(Path(gs[sel]["picture"]))[0]
        cols = [0, 77, 159]
        got = [layer2_column(e, 152 + x, 56, 120) for x in cols]
        check("picture loaded from the card into Layer 2 (sd_load_l2w)",
              got == [pic[x * 120:(x + 1) * 120] for x in cols])

        # moving: each press is one step, the picture follows when the cursor rests
        start = e.log_cursor()
        e.press("A", hold_frames=2, release_frames=2)
        check("A moves down one", e.run_until("arcade: sel=1", 50))
        check("its picture follows", e.run_until("arcade: shown=1", 50))
        e.run(3)
        pic1 = arcade.picture(Path(gs[1]["picture"]))[0]
        check("... and is the second game's", layer2_column(e, 152 + 80, 56, 120) == pic1[80 * 120:81 * 120])
        e.screenshot(SHOTS / "menu_second.png")
        e.press("Q", hold_frames=2, release_frames=2)
        check("Q moves up", e.run_until("arcade: sel=0", 50))
        e.press("Q", hold_frames=2, release_frames=2)
        check("and stops at the top", not e.run_until("arcade: sel=", 40))
        e.press("DOWN", hold_frames=2, release_frames=2)       # Kempston
        check("the joystick moves too", e.run_until("arcade: sel=1", 50))
        e.press("UP", hold_frames=2, release_frames=2)
        e.run_until("arcade: sel=0", 50)
        e.press("S")
        check("S sorts by plays", e.run_until("arcade: sort=1", 50))
        e.press("S")
        check("S again: A-Z", e.run_until("arcade: sort=0", 50))
        e.run(10)
        sel = 0

        # every game from the menu, and back with reset
        for n, g in enumerate(gs):
            if n:
                e.reset(hard=True)
                sel = arcade.menu_ready(e, 3000)
                check(f"reset: back in the menu on the last game played ({gs[n - 1]['title']})",
                      sel == gs[n - 1]["index"], str(sel))
            start = e.log_cursor()
            ok = arcade.menu_choose(e, g, len(gs), 3000, sel=sel)
            text = e.log_text(since=start)
            check(f"{g['title']} ({g['machine']}) starts from the menu",
                  ok and f"launch={g['index']} " in text, text[-200:])
            check(f"{g['title']}: play counted", last(r"launch=\d+ .* plays=(\d+)", text) == "1")
            e.run(60)
            e.screenshot(SHOTS / f"game_{g['project']}.png")

        # most played first: everything has one play, so the ties stay A-Z;
        # play the last game again and it moves to the top
        e.reset(hard=True)
        sel = arcade.menu_ready(e, 3000)
        g = gs[-1]
        start = e.log_cursor()
        ok = arcade.menu_choose(e, g, len(gs), 3000, sel=sel)
        check(f"{g['title']} a second time: two plays",
              ok and last(r"launch=\d+ .* plays=(\d+)", e.log_text(since=start)) == "2")
        e.reset(hard=True)
        arcade.menu_ready(e, 3000)
        start = e.log_cursor()
        e.press("S")
        e.run_until("arcade: sort=1", 50)
        e.run(1)
        check("most played first", last(r"sort=1 first=(\d+)", e.log_text(since=start)) == str(g["index"]))
        e.run(10)
        e.screenshot(SHOTS / "menu_sorted.png")

        # BREAK leaves to NextBASIC (autoexec.bas STOPs on 255)
        e.reset(hard=True)
        arcade.menu_ready(e, 3000)
        e.press("CAPS+SPACE")
        check("BREAK quits the menu", e.run_until("arcade: quit", 50))
        e.run(100)
        check("... back to NextBASIC (ROM), menu gone",
              e.status()["pc"] < 0x4000 and e.peek(32767) == 255 and "arcade: ready" not in e.log())

        if "--gif" in sys.argv:
            e.reset(hard=True)
            arcade.menu_ready(e, 3000)
            e.run(20)
            gif(e, gs, ROOT / "docs" / "images" / "arcade.gif")
            print("  wrote docs/images/arcade.gif")
        if failures:
            print("\n".join(e.debug_log[-25:]))

print(f"{'FAIL' if failures else 'PASS'}: arcade")
sys.exit(1 if failures else 0)
