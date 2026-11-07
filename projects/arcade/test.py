"""Test for the arcade menu (run by `./zx test arcade` and ./zx selftest).

Builds the SD image (every game with an arcade.json, the menu, NextZXOS),
boots a copy of it on the Next and checks: NextZXOS runs autoexec.bas into
the menu; the list, the picture (bytes in Layer 2 = the file on the card,
sdfile's sd_load_l2w) and the details follow the cursor; S sorts by plays;
then every game is chosen from the menu and must reach its title (its
"ready" log line), and a reset brings the menu back with that game selected
and its play counted (stats.dat written with sd_save, read back with sd_load).
The sort checks go by the menu's own log lines (arcade.set_sort), so they
hold for any number of games. Then a card padded to 22+ entries
(arcade.padded_image) checks scrolling: to the last entry and back, the bar
on the right row, the picture, starting the last entry, and the menu opening
scrolled to it after a reset.

    .venv/bin/python projects/arcade/test.py [--gif] [--no-build] [--all]

--gif also records docs/images/arcade.gif.

Starting every game from the menu is the slow part. A launch that worked is
recorded in the shared build cache under a key of everything that decides
it (arcade.launch_key: the menu, the card, the launch line, the game's
machine and program bytes); by default only games whose key hasn't been
verified are started, plus always the first and last in the list. Any
change to a game, the menu or the card means that game is started again,
so a game that doesn't start from the menu is still always caught.
--all (or ZX_ARCADE_ALL=1) starts every game.
"""
import os
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
        # S switches the order; go by the menu's log, not by counting presses
        check("S sorts by plays (none yet: A-Z order, first game first)", arcade.set_sort(e, 1) == 0)
        check("S again: A-Z", arcade.set_sort(e, 0) == 0)
        e.run(10)
        sel = 0

        # games from the menu, and back with reset: the ones whose launch isn't
        # verified for these exact bytes, the first and the last (see above)
        menu_bin = ROOT / "projects" / "arcade" / "build" / "arcade.bin"
        keys = [arcade.launch_key(g, menu_bin) for g in gs]
        everything = "--all" in sys.argv or os.environ.get("ZX_ARCADE_ALL") == "1"
        todo = [n for n, g in enumerate(gs)
                if everything or n in (0, len(gs) - 1) or not arcade.launch_verified(keys[n])]
        print(f"  starting {len(todo)} of {len(gs)} games from the menu"
              f"{'' if everything else ' (the others: verified with these bytes before; --all for every one)'}")
        prev = None
        for n in todo:
            g = gs[n]
            if prev is not None:
                e.reset(hard=True)
                sel = arcade.menu_ready(e, 3000)
                check(f"reset: back in the menu on the last game played ({gs[prev]['title']})",
                      sel == gs[prev]["index"], str(sel))
            start = e.log_cursor()
            ok = arcade.menu_choose(e, g, len(gs), 3000, sel=sel)
            text = e.log_text(since=start)
            started = ok and f"launch={g['index']} " in text
            check(f"{g['title']} ({g['machine']}) starts from the menu", started, text[-200:])
            counted = last(r"launch=\d+ .* plays=(\d+)", text) == "1"
            check(f"{g['title']}: play counted", counted)
            if started and counted:
                arcade.mark_launch_verified(keys[n], g["title"])
            e.run(60)
            e.screenshot(SHOTS / f"game_{g['project']}.png")
            prev = n

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
        first = arcade.set_sort(e, 1)
        check("most played first", first == g["index"], f"first={first}, want {g['index']}")
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

# more games than the menu shows at once: scrolling, the bar, pictures, starting one from below
with tempfile.TemporaryDirectory() as tmp:
    sd = Path(tmp) / "many.mmc"
    total = min(32, max(22, len(gs) + 6))
    full = arcade.padded_image(Path(m["image"]), sd, total, gs)
    with Emulator(machine="next", sd=sd) as e:
        sel = arcade.menu_ready(e, 3000)
        r = arcade.menu_line(e, r"ready games=(\d+) .*rows=(\d+)", 0)
        rows = int(r.group(2)) if r else 0
        check(f"a card with {total} entries: more than the {rows} rows shown",
              r is not None and int(r.group(1)) == total and total > rows)
        pos = top = 0
        for _ in range(total + 2):
            start = e.log_cursor()
            e.press("A", hold_frames=2, release_frames=2)
            mm = arcade.menu_line(e, r"arcade: sel=(\d+) pos=(\d+) top=(\d+)", start, 40)
            if not mm:
                break
            sel, pos, top = map(int, mm.groups())
        check("A goes down to the last entry and stops there", pos == total - 1 and sel == total - 1)
        check("the list scrolled with it", top == total - rows, f"top={top}")
        e.run_until(f"arcade: shown={sel}", 50)
        e.run(2)
        bar_y = 44 + (pos - top) * 12 + 1
        check("the bar is on the bottom row, the top row isn't the bar",
              layer2_column(e, 17, bar_y, 1)[0] == 0x1F and layer2_column(e, 17, 45, 1)[0] != 0x1F)
        pic = arcade.picture(Path(full[sel]["picture"]))[0]
        check("its picture is there", all(layer2_column(e, 152 + x, 56, 120) == pic[x * 120:(x + 1) * 120]
                                          for x in (0, 80, 159)))
        e.screenshot(SHOTS / "menu_scrolled.png")
        for _ in range(total + 2):
            start = e.log_cursor()
            e.press("Q", hold_frames=2, release_frames=2)
            mm = arcade.menu_line(e, r"arcade: sel=(\d+) pos=(\d+) top=(\d+)", start, 40)
            if not mm:
                break
            sel, pos, top = map(int, mm.groups())
        check("Q goes back up to the first and scrolls back", pos == 0 and top == 0)
        g = full[-1]
        ok = arcade.menu_choose(e, g, total, 3000, sel=sel)
        check(f"the last entry starts its game ({gs[g['source']]['title']})",
              ok and f"launch={g['index']} " in e.log_text(since=0))
        e.reset(hard=True)
        sel = arcade.menu_ready(e, 3000)
        r = arcade.menu_line(e, r"ready games=\d+ sort=\d sel=(\d+) pos=(\d+) top=(\d+)", 0)
        check("reset: the menu opens scrolled to it",
              sel == g["index"] and r is not None and int(r.group(3)) == total - rows)
        if failures:
            print("\n".join(e.debug_log[-15:]))

print(f"{'FAIL' if failures else 'PASS'}: arcade")
sys.exit(1 if failures else 0)
