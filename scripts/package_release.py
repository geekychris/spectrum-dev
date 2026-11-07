#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>
"""Package the games for download: one zip per game with an arcade.json
(the .tap, plus a .sna snapshot, for a 48K game; the .nex for a Next game;
and README.txt), an all-games zip, and manifest.json, which the website's
/games/ page is made from (geekychris-site: scripts/gen-games-page.py).

  ./zx release [--no-build] [--tag games-YYYY-MM-DD] [--out dist/release]

Builds every game first (bin/zx-make), unless --no-build. The 48K
snapshots are taken in the emulator at the moment the game's code starts
(as the arcade does), so ZEsarUX must be built (./zx setup).

The all-games zip has every game, the arcade's /arcade/ folder and
autoexec.bas for a real Next's card, and how to build the arcade SD image
(./zx arcade). It does not hold NextZXOS: its licence wants the
distribution passed on whole, so the README links to it instead.

Then: gh release create <tag> dist/release/*.zip dist/release/manifest.json
"""
import argparse
import datetime
import json
import shutil
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools" / "zxdev" / "src"))
from zxdev import arcade  # noqa: E402

REPO = "geekychris/spectrum-dev"
DISTRO = "https://www.specnext.com/latestdistro/"
BUNDLE = "spectrum-games.zip"

MACHINES = {
    "48k": ("48K", "A ZX Spectrum 48K, or a 128K, +2, +2A, +3 or Spectrum Next (in its 48K mode)."
                   " On a 128K or later the AY chip plays the music."),
    "next": ("Next", "A ZX Spectrum Next (or an N-Go / Next-compatible board) with NextZXOS."),
}

RUN_48K = """\
To run it
---------
In an emulator: open {id}.tap (or the {id}.sna snapshot) in Fuse, ZEsarUX,
Spectaculator or SpecEmu, as a 48K or 128K Spectrum. Fuse: File > Open.
More of my games, and how to run them: https://geekychris.github.io/games/

On a real Spectrum:
- DivMMC / DivIDE (esxDOS): copy {id}.tap or {id}.sna to the SD card, press
  NMI and pick it in the browser (or .tapein {id}.tap, then LOAD "").
- Tape: play {id}.tap into the EAR socket from a TZXDuino / CASDuino, or turn
  it into audio (e.g. tape2wav from fuse-utils, or the PlayTZX/TZX apps) and
  type LOAD "" (J, then SYMBOL SHIFT+P twice, ENTER).
- Spectrum Next: copy {id}.sna or {id}.tap to the card and open it from the
  NextZXOS browser.
"""

RUN_NEXT = """\
To run it
---------
On a Spectrum Next: copy {id}.nex to the SD card and open it from the
NextZXOS browser (or type .nexload {id}.nex).

In an emulator:
- CSpect: CSpect -zxnext -mmc=<folder>/ {id}.nex
- ZEsarUX: machine ZX Spectrum Next (TBBlue), then File > Smart load {id}.nex

It needs the Next's hardware (Layer 2, sprites, the Copper or 28 MHz), so it
does not run on an older Spectrum.
"""


def readme(g, files):
    label, needs = MACHINES[g["machine"]]
    lines = [g["title"], "=" * len(g["title"]), "", g["description"], "",
             "Controls: " + g["controls"], "",
             "Machine:  " + ("ZX Spectrum 48K/128K" if g["machine"] == "48k" else "Spectrum Next"),
             "Needs:    " + needs, "", "Files: " + ", ".join(files), "",
             (RUN_48K if g["machine"] == "48k" else RUN_NEXT).format(id=g["project"]),
             f"Source: https://github.com/{REPO}/tree/main/projects/{g['project']}",
             "MIT licence."]
    return "\r\n".join(lines) + "\r\n"


def snapshot(g, cache):
    """the 48K game's .sna, taken again only when its .tap is newer"""
    tap = Path(g["program"])
    sna = cache / f"{g['project']}.sna"
    if not sna.exists() or sna.stat().st_mtime < tap.stat().st_mtime:
        print(f"  snapshot {tap.name}")
        arcade.snapshot_48k(tap, sna)
    return sna


def bundle_readme(gs, tag):
    rows = "\n".join(f"  games/{g['project']}.{'nex' if g['machine'] == 'next' else 'tap'}".ljust(34)
                     + f"{g['title']} ({'Spectrum Next' if g['machine'] == 'next' else '48K/128K'})" for g in gs)
    return f"""\
ZX Spectrum games ({tag})
{'=' * (19 + len(tag))}

Every game from https://github.com/{REPO}, MIT licence.

games/      each game: the .tap (and a .sna snapshot) for a 48K game, the .nex
            for a Spectrum Next game. Each game's own zip on the release page has
            a README with its controls and how to run it.
{rows}

next-card/  the Spectrum Arcade (a game menu for the Next) for a real Next's
            SD card: copy arcade/ to the card's root and nextzxos/autoexec.bas
            over the card's own (keep a copy of yours), and the Next boots into
            the menu. Reset brings it back. Untested on real hardware; it runs
            in ZEsarUX.

The arcade SD image
-------------------
The image itself is not included: it is built on NextZXOS, whose licence
("The Next License", SpecNext Ltd) wants the distribution passed on whole.
Get NextZXOS from {DISTRO}
To build the image for an emulator, from a checkout of the repository:

  git clone https://github.com/{REPO} && cd spectrum-dev
  ./zx setup             # the toolchain (Docker) and ZEsarUX, which ships NextZXOS
  ./zx arcade            # build every game and the menu, write build/arcade/arcade.mmc,
                         # and open it in ZEsarUX
  ./zx arcade --image-only    # just the image

See docs/arcade.md and projects/arcade/README.md in the repository.
"""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=str(ROOT / "dist" / "release"))
    ap.add_argument("--tag", default="games-" + datetime.date.today().isoformat())
    ap.add_argument("--no-build", action="store_true", help="use the programs already in projects/*/build/")
    opt = ap.parse_args()
    out = Path(opt.out)
    gs = arcade.games()
    if not opt.no_build:
        print(f"building {len(gs)} games and the arcade menu")
        arcade.build_projects([g["project"] for g in gs] + ["arcade"])
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    cache = ROOT / "build" / "release"
    cache.mkdir(parents=True, exist_ok=True)
    url = lambda name: f"https://github.com/{REPO}/releases/download/{opt.tag}/{name}"

    games, card = [], {}
    bz = zipfile.ZipFile(out / BUNDLE, "w", zipfile.ZIP_DEFLATED)
    for i, g in enumerate(gs):
        gid, prog = g["project"], Path(g["program"])
        label, needs = MACHINES[g["machine"]]
        entry = {"id": gid, "title": g["title"], "description": g["description"], "controls": g["controls"],
                 "machine": label, "needs": needs, "packages": {}}
        games.append(entry)
        if not prog.exists():
            print(f"  {gid}: {prog.name} not built")
            continue
        files = {f"{gid}{prog.suffix}": prog}
        if g["machine"] == "48k":
            files[f"{gid}.sna"] = snapshot(g, cache)
        card[arcade.game_file(g)] = (files.get(f"{gid}.sna") or prog).read_bytes()
        data, _ = arcade.picture(Path(g["picture"]))
        card[f"pic{i:02d}.l2"] = data
        name = f"{gid}-spectrum.zip"
        with zipfile.ZipFile(out / name, "w", zipfile.ZIP_DEFLATED) as z:
            for fn, src in files.items():
                z.write(src, f"{gid}/{fn}")
                bz.write(src, f"games/{fn}")
            z.writestr(f"{gid}/README.txt", readme(g, list(files)))
        entry["packages"]["spectrum"] = {"file": name, "bytes": (out / name).stat().st_size, "url": url(name)}
        entry["files"] = {"tap" if fn.endswith(".tap") else fn.rsplit(".", 1)[1]: fn for fn in files}

    # the arcade for a real Next's card (our files only, no NextZXOS)
    built = [g for g in gs if Path(g["program"]).exists()]
    menu = ROOT / "projects" / "arcade" / "build" / "arcade.bin"
    if len(built) == len(gs) and menu.exists():
        bz.writestr("next-card/arcade/menu.bin", arcade.nextbasic.plus3dos_code(menu.read_bytes(), 32768))
        bz.writestr("next-card/arcade/games.dat", arcade.games_dat(gs))
        bz.writestr("next-card/arcade/stats.dat", arcade.stats_dat([]))
        for fn, data in card.items():
            bz.writestr(f"next-card/arcade/{fn}", data)
        bz.writestr("next-card/nextzxos/autoexec.bas", arcade.autoexec(gs))
    else:
        print("  the Next card's arcade/ left out: not every game (or the menu) is built")
    bz.writestr("README.txt", bundle_readme(gs, opt.tag).replace("\n", "\r\n"))
    bz.close()

    manifest = {"repo": REPO, "tag": opt.tag, "platforms": ["spectrum"],
                "bundles": {"spectrum": url(BUNDLE)},
                "bundle_bytes": {"spectrum": (out / BUNDLE).stat().st_size},
                "games": games}
    (out / "manifest.json").write_text(json.dumps(manifest, indent=1) + "\n")
    print(f"{len(games)} games, {sum(1 for g in games if g['packages'])} packaged, + {BUNDLE} -> {out}")


if __name__ == "__main__":
    main()
