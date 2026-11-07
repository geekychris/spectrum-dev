"""The arcade: every game with a projects/<p>/arcade.json, on an SD card for the Next.

    ./zx arcade [--no-build] [--headless]    build the games + menu, make build/arcade/arcade.mmc, open it

The SD image holds NextZXOS (from ZEsarUX's tbblue.mmc, trimmed) and /arcade/:
the menu (projects/arcade, machine code), games.dat (the list), pictures,
stats.dat (plays, sort order) and the games: .nex files as they are, 48K
.taps as .sna snapshots taken at their entry point. nextzxos/autoexec.bas
loads the menu, runs it with USR and starts the game it returns the way
NextZXOS's own browser does (.nexload, or SPECTRUM for a snapshot). Reset
brings the menu back.

A game's arcade.json (machine and file come from its Makefile):

    {"title": "Nova Defense",               # max 20 characters
     "description": "One or two sentences.",  # max ~160, wrapped to the menu
     "controls": "O/P move, SPACE fire",      # one line, max 40
     "picture": "screenshots/play.png",       # a ./zx shot (352x304 or 704x608 with border)
     "ready": "nova: ready"}                  # log line at the title (tests wait for it)
"""
from __future__ import annotations

import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

from PIL import Image

from .emu import Emulator, repo_root

ROOT = repo_root()
sys.path.insert(0, str(ROOT / "sdk" / "tools"))
import fatimg  # noqa: E402
import nextbasic  # noqa: E402

OUT = ROOT / "build" / "arcade"
MAX_GAMES = 32
REC = 256
PIC_W, PIC_H = 160, 120
CHOICE_ADDR = 32767                      # where the menu leaves the chosen game
TRIM = ["games", "demos", "docs", "src", "rpi", "tools", "nextzxos/cpmbase.p3d"]


class ArcadeError(RuntimeError):
    pass


def base_image() -> Path:
    return ROOT / "third_party" / "zesarux" / "src" / "tbblue.mmc"


def _make_var(makefile: Path, var: str) -> str | None:
    m = re.search(rf"^{var}\s*:?=\s*(\S+)", makefile.read_text(), re.M)
    return m.group(1) if m else None


def games(root: Path | None = None) -> list[dict]:
    """The registered games, by title, each with its project, machine, program path and fields."""
    root = root or ROOT
    out, shorts = [], set()
    for meta in sorted((root / "projects").glob("*/arcade.json")):
        proj = meta.parent
        try:
            g = json.loads(meta.read_text())
        except json.JSONDecodeError as ex:
            raise ArcadeError(f"{meta}: {ex}") from None
        missing = [k for k in ("title", "description", "controls", "picture", "ready") if not g.get(k)]
        if missing:
            raise ArcadeError(f"{meta}: missing {', '.join(missing)}")
        name = _make_var(proj / "Makefile", "NAME") or proj.name
        target = _make_var(proj / "Makefile", "TARGET") or "zx"
        short = re.sub(r"[^a-z0-9]", "", proj.name.lower())[:8] or "game"
        while short in shorts:
            short = short[:7] + str(len(shorts) % 10)
        shorts.add(short)
        out.append({
            "project": proj.name, "title": g["title"][:20], "description": g["description"],
            "controls": g["controls"][:40], "ready": g["ready"],
            "machine": "next" if target == "zxn" else "48k",
            "program": str(proj / "build" / f"{name}.{'nex' if target == 'zxn' else 'tap'}"),
            "picture": str(proj / g["picture"]), "short": short,
        })
    out.sort(key=lambda g: g["title"].lower())
    if len(out) > MAX_GAMES:
        raise ArcadeError(f"{len(out)} games; the menu holds {MAX_GAMES}")
    return out


def build_projects(names, log=print):
    for n in names:
        log(f"  build {n}")
        r = subprocess.run([str(ROOT / "bin" / "zx-make"), "-C", str(ROOT / "projects" / n)],
                           capture_output=True, text=True)
        if r.returncode:
            raise ArcadeError(f"build of {n} failed:\n{r.stdout[-2000:]}{r.stderr[-2000:]}")


def snapshot_48k(tap: Path, out: Path):
    """A .sna of a 48K tape at the moment its code starts: the first instruction
    the CPU runs in RAM (the BASIC loader's USR), before any of it has run."""
    with Emulator(machine="48k") as e:
        e.load(tap)
        e.break_if("PC>23295")
        r = e.continue_(3000)
        if (r or {}).get("why") != "breakpoint":
            raise ArcadeError(f"{tap.name}: its code never started ({r})")
        e.delete()
        e.save_state(out)
    Path(str(out) + ".beam").unlink(missing_ok=True)


def picture(src: Path) -> tuple[bytes, Image.Image]:
    """A screenshot -> 160 x 120 Layer 2 bytes (RRRGGGBB, column-major) + the
    picture as it will look. The 320 x 240 middle of the 352 x 304 frame
    (all of a 320 x 256 Layer 2 game, a 48K screen with some border), halved."""
    img = Image.open(src).convert("RGB")
    if img.size != (352, 304):
        img = img.resize((352, 304), Image.NEAREST if img.size == (704, 608) else Image.BILINEAR)
    img = img.crop((16, 32, 336, 272)).resize((PIC_W, PIC_H), Image.BOX)
    px = img.load()
    data = bytearray(PIC_W * PIC_H)
    shown = Image.new("RGB", (PIC_W, PIC_H))
    for x in range(PIC_W):
        for y in range(PIC_H):
            r, g, b = px[x, y]
            r3, g3, b2 = (r * 7 + 127) // 255, (g * 7 + 127) // 255, (b * 3 + 127) // 255
            v = r3 << 5 | g3 << 2 | b2
            if v == 0xE3:                     # the global transparent colour
                v = 0xE2
            data[x * PIC_H + y] = v
            shown.putpixel((x, y), ((v >> 5) * 255 // 7, (v >> 2 & 7) * 255 // 7, (v & 3) * 255 // 3))
    return bytes(data), shown


def wrap(text: str, width: int = 26, lines: int = 4) -> list[str]:
    out, cur = [], ""
    for word in text.split():
        if cur and len(cur) + 1 + len(word) > width:
            out.append(cur)
            cur = word
        else:
            cur = f"{cur} {word}" if cur else word
    if cur:
        out.append(cur)
    if len(out) > lines:
        out = out[:lines]
        out[-1] = out[-1][:width - 3].rstrip() + "..."
    return [line[:width] for line in out]


def games_dat(gs: list[dict]) -> bytes:
    out = bytearray(b"ZXARC1" + bytes([len(gs), 0]))
    for g in gs:
        rec = bytearray(REC)
        rec[0:24] = g["title"].encode("latin-1", "replace")[:23].ljust(24, b"\0")
        rec[24] = 1 if g["machine"] == "next" else 0
        rec[32:80] = g["controls"].encode("latin-1", "replace")[:47].ljust(48, b"\0")
        desc = "\n".join(wrap(g["description"])).encode("latin-1", "replace")[:175]
        rec[80:256] = desc.ljust(176, b"\0")
        out += rec
    return bytes(out)


def stats_dat(plays: list[int], sort: int = 0, last: int = 0) -> bytes:
    plays = (plays + [0] * MAX_GAMES)[:MAX_GAMES]
    return b"".join(min(p, 0xFFFF).to_bytes(2, "little") for p in plays) + bytes([sort, last])


def read_stats(data: bytes) -> dict:
    plays = [int.from_bytes(data[i * 2:i * 2 + 2], "little") for i in range(MAX_GAMES)]
    return {"plays": plays, "sort": data[MAX_GAMES * 2], "last": data[MAX_GAMES * 2 + 1]}


def game_file(g: dict) -> str:
    return f"{g['short']}.{'nex' if g['machine'] == 'next' else 'sna'}"


def autoexec(gs: list[dict]) -> bytes:
    """NextBASIC: load + run the menu, then start what it chose (line 1000 + 10 * n)."""
    lines = [f'10 CLEAR {CHOICE_ADDR - 1}: LOAD "c:/arcade/menu.bin" CODE 32768',
             f"20 RANDOMIZE USR 32768: IF PEEK {CHOICE_ADDR}=255 THEN STOP",
             f"30 GO TO 1000+10*PEEK {CHOICE_ADDR}"]
    clear = "LAYER CLEAR: SPRITE CLEAR: PALETTE CLEAR: CLS"
    for i, g in enumerate(gs):
        if g["machine"] == "next":     # dot commands take esxDOS paths (no "c:")
            lines.append(f"{1000 + 10 * i} {clear}: .nexload /arcade/{game_file(g)}")
        else:                          # SPECTRUM "x.sna": NextZXOS's 48K snapshot loader
            lines.append(f'{1000 + 10 * i} RUN AT 0: {clear}: SPECTRUM "c:/arcade/{game_file(g)}"')
    return nextbasic.plus3dos_basic(nextbasic.tokenize("\n".join(lines)), autostart=10)


def make_image(gs: list[dict], menu_bin: Path, files: dict, out: Path, keep_stats_from: Path | None = None):
    """The SD image: trimmed NextZXOS + /arcade/ + our autoexec.bas."""
    tree = fatimg.drop(fatimg.read_image(base_image()), TRIM)
    cfg = fatimg.find(tree, "machines/next/config.ini")
    if cfg:   # timing=8 runs the video mode test on every boot: pick mode 0
        tree[cfg] = re.sub(rb"timing=\d", b"timing=0", tree[cfg])
    fatimg.put(tree, "nextzxos/autoexec.bas", autoexec(gs))
    fatimg.put(tree, "arcade/menu.bin", nextbasic.plus3dos_code(menu_bin.read_bytes(), 32768))
    fatimg.put(tree, "arcade/games.dat", games_dat(gs))
    for name, data in files.items():
        fatimg.put(tree, f"arcade/{name}", data)
    # plays and sort order survive a rebuild (matched by title)
    plays, sort, last = [0] * len(gs), 0, 0
    if keep_stats_from and keep_stats_from.exists():
        try:
            old = fatimg.FatImage(keep_stats_from.read_bytes())
            st = read_stats(old.read("arcade/stats.dat"))
            raw = old.read("arcade/games.dat")
            titles = [raw[8 + i * REC:8 + i * REC + 24].split(b"\0")[0].decode("latin-1") for i in range(raw[6])]
            for i, t in enumerate(titles):
                for j, g in enumerate(gs):
                    if g["title"] == t:
                        plays[j] = st["plays"][i]
                        if st["last"] == i:
                            last = j
            sort = st["sort"]
        except (FileNotFoundError, ValueError, IndexError, KeyError):
            pass
    fatimg.put(tree, "arcade/stats.dat", stats_dat(plays, sort, last))
    tmp = out.with_suffix(".tmp")
    fatimg.write_image(tree, tmp, size_mb=32)
    tmp.replace(out)


def docs(gs: list[dict]) -> str:
    rows = ["# Arcade games", "",
            "Generated by `./zx arcade` from each project's `arcade.json`; the menu on the",
            "Next shows the same list (see the Arcade section of the README).", "",
            "| | Game | Machine | Controls |", "|---|---|---|---|"]
    for g in gs:
        pic = f"images/arcade/{g['short']}.png"
        rows.append(f"| ![]({pic}) | **[{g['title']}](../projects/{g['project']}/README.md)**<br>"
                    f"{g['description']} | {'Spectrum Next' if g['machine'] == 'next' else '48K'} "
                    f"| {g['controls']} |")
    return "\n".join(rows) + "\n"


def build(do_build: bool = True, log=print, out_dir: Path = OUT) -> dict:
    """Build everything and write out_dir/arcade.mmc (+ games.json, pictures). Returns the manifest."""
    gs = games()
    if not gs:
        raise ArcadeError("no projects/*/arcade.json")
    if do_build:
        build_projects([g["project"] for g in gs] + ["arcade"], log)
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "pics").mkdir(exist_ok=True)
    menu = ROOT / "projects" / "arcade" / "build" / "arcade.bin"
    if not menu.exists():
        raise ArcadeError("menu not built (./zx build arcade)")
    files = {}
    docs_pics = ROOT / "docs" / "images" / "arcade"
    docs_pics.mkdir(parents=True, exist_ok=True)
    for i, g in enumerate(gs):
        prog = Path(g["program"])
        if not prog.exists():
            raise ArcadeError(f"{g['project']}: {prog.name} not built")
        if g["machine"] == "48k":
            sna = out_dir / f"{g['short']}.sna"
            if not sna.exists() or sna.stat().st_mtime < prog.stat().st_mtime:
                log(f"  snapshot {prog.name} -> {sna.name}")
                snapshot_48k(prog, sna)
            files[game_file(g)] = sna.read_bytes()
        else:
            files[game_file(g)] = prog.read_bytes()
        data, shown = picture(Path(g["picture"]))
        files[f"pic{i:02d}.l2"] = data
        shown.save(out_dir / "pics" / f"{g['short']}.png")
        shown.resize((PIC_W * 2, PIC_H * 2), Image.NEAREST).save(docs_pics / f"{g['short']}.png")
        g["index"] = i
        g["image_picture"] = str(out_dir / "pics" / f"{g['short']}.png")
    image = out_dir / "arcade.mmc"
    make_image(gs, menu, files, image, keep_stats_from=image)
    shutil.copy(ROOT / "projects" / "arcade" / "build" / "arcade.map", out_dir / "arcade.map")
    (ROOT / "docs" / "arcade.md").write_text(docs(gs))
    manifest = {"image": str(image), "games": gs}
    (out_dir / "games.json").write_text(json.dumps(manifest, indent=2))
    log(f"  {len(gs)} games -> {image}")
    return manifest


def manifest() -> dict:
    """The last build's games.json, else the games as registered (not built)."""
    p = OUT / "games.json"
    if p.exists():
        return json.loads(p.read_text())
    return {"image": None, "games": games()}


def find_game(gs: list[dict], name: str) -> dict:
    n = name.lower()
    for g in gs:
        if n in (g["project"].lower(), g["title"].lower(), g["short"]):
            return g
    raise ArcadeError(f"no game {name!r}; one of {', '.join(g['project'] for g in gs)}")


def menu_ready(emu: Emulator, max_frames: int = 3000) -> int | None:
    """Run until the menu is up; the game number it selected, or None."""
    start = emu.log_cursor()
    if not emu.run_until("arcade: ready", max_frames):
        return None
    emu.run(1)                                  # the rest of the line
    m = re.findall(r"arcade: ready games=\d+ sort=\d sel=(\d+)", emu.log_text(since=start))
    return int(m[-1]) if m else None


def menu_select(emu: Emulator, index: int, sel: int, count: int) -> bool:
    """Move the menu's cursor (now on game `sel`) to game `index`, watching its
    "arcade: sel=" lines: down until it gets there or stops at the end, then up."""
    key = "A"
    for _ in range(2 * count + 2):
        if sel == index:
            return True
        start = emu.log_cursor()
        emu.press(key, hold_frames=2, release_frames=2)
        if emu.run_until("arcade: sel=", 50, since=start):
            emu.run(1)                          # the rest of the line
            sel = int(re.findall(r"arcade: sel=(\d+)", emu.log_text(since=start))[-1])
        else:
            key = "Q" if key == "A" else "A"    # at an end of the list
    return sel == index


def menu_choose(emu: Emulator, game: dict, count: int, max_frames: int = 3000) -> bool:
    """Boot (or wait for) the menu, pick `game`, play it: True once its ready line shows."""
    sel = menu_ready(emu, max_frames)
    if sel is None or not menu_select(emu, game["index"], sel, count):
        return False
    emu.press("ENTER")
    return emu.run_until(game["ready"], max_frames)
