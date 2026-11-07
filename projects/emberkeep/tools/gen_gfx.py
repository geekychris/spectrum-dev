"""The original's tile sheet (art/tilesheet.png, as in amiga_mcp/examples/
emberkeep/art) -> src/gfx_data.c / .h for the Next.

The sheet is 16 x 16 pixel art drawn ~6x larger on a 20 x 8 grid. The
original bakes it into 320 x 128 by nearest-neighbour and snaps every pixel to
its 16 colours; here the same samples keep their own colours (RGB333, the
Next's 9-bit palette), with the original's 16 at indices 0-15 so pens mean
what they meant there (text, the HUD).

    python3 tools/gen_gfx.py

Writes:
  gfx_pal[256]        the palette (9-bit), Layer 2 and sprites
  gfx_tiles[n][256]   terrain tiles for Layer 2 320 x 256: column-major
                      (16 columns of 16 rows), see TILE_* in gfx_data.h
  src/gfx_sprites.asm sprite patterns (16 rows of 16) in 16K bank SPR_BANK
                      (the .nex loads them there; spr_patterns_from_pages
                      uploads them), the cell's background (black reached
                      from the edge) = 0xE3, transparent
"""
from pathlib import Path

import numpy as np
from PIL import Image

HERE = Path(__file__).resolve().parents[1]
SHEET = HERE / "art" / "tilesheet.png"
TRANSPARENT = 0xE3
SPR_BANK = 20                   # the sprite patterns' 16K bank (pages 40, 41)

# render.cpp's PALETTE
ORIGINAL = [(0, 0, 0), (240, 240, 240), (168, 48, 48), (128, 88, 40), (64, 208, 224), (224, 80, 192),
            (240, 224, 64), (64, 200, 64), (72, 72, 80), (240, 64, 64), (240, 160, 64), (80, 128, 240),
            (128, 128, 128), (168, 224, 128), (240, 168, 200), (40, 40, 40)]

# (name, sheet row, sheet column): render.cpp's SR_ / SC_ layout
TILES = [("FLOOR", 0, 0), ("WALL_A", 0, 1), ("WALL_B", 0, 2), ("RUNE", 0, 3), ("OIL", 0, 4),
         ("HAZARD", 0, 5), ("TORCH", 0, 6), ("CHEST", 0, 7), ("PORTAL_LOCKED", 1, 0), ("PORTAL_OPEN", 1, 1)]
SPRITES = [("POSE_UP", 4, 0), ("POSE_RIGHT", 4, 1), ("POSE_DOWN", 4, 2), ("POSE_LEFT", 4, 3)]
SPRITES += [(f"ENEMY_{k}{f}", row, f) for k, row in (("A", 5), ("B", 6), ("C", 7)) for f in range(8)]


def to333(c):
    return tuple(min(7, (int(v) * 7 + 127) // 255) for v in c)


def rgb9(c3):
    return c3[0] << 6 | c3[1] << 3 | c3[2]


def sample():
    """The sheet at 320 x 128, nearest-neighbour (as bake_tilesheet.py)."""
    im = Image.open(SHEET).convert("RGB")
    return np.asarray(im.resize((320, 128), Image.NEAREST))


def cell(sheet, row, col):
    return sheet[row * 16:(row + 1) * 16, col * 16:(col + 1) * 16]


def background(c):
    """Black pixels connected to the cell's edge: the sprite's see-through part."""
    dark = (np.asarray(c).sum(-1) < 40)
    bg = np.zeros_like(dark)
    todo = [(y, x) for y in range(16) for x in range(16) if (y in (0, 15) or x in (0, 15)) and dark[y, x]]
    for p in todo:
        bg[p] = True
    while todo:
        y, x = todo.pop()
        for ny, nx in ((y + 1, x), (y - 1, x), (y, x + 1), (y, x - 1)):
            if 0 <= ny < 16 and 0 <= nx < 16 and dark[ny, nx] and not bg[ny, nx]:
                bg[ny, nx] = True
                todo.append((ny, nx))
    return bg


def main():
    sheet = sample()
    cells = [cell(sheet, r, c) for _, r, c in TILES + SPRITES]
    pal3 = [to333(c) for c in ORIGINAL]
    for c in cells:
        for px in c.reshape(-1, 3):
            p = to333(px)
            if p not in pal3:
                pal3.append(p)
    if len(pal3) > 255:
        raise SystemExit(f"{len(pal3)} colours: more than the palette holds")
    index = {p: i if i < TRANSPARENT else i + 1 for i, p in enumerate(pal3)}   # 0xE3 stays free
    pal = [0] * 256
    for p, i in index.items():
        v = rgb9(p)
        pal[i] = v ^ 2 if (v >> 1) == TRANSPARENT else v     # never Layer 2's see-through colour

    def idx(c):
        return np.array([[index[to333(px)] for px in row] for row in c], np.uint8)

    tiles = [idx(cell(sheet, r, c)).T.reshape(-1) for _, r, c in TILES]        # column-major
    sprites = []
    for _, r, c in SPRITES:
        cc = cell(sheet, r, c)
        a = idx(cc)
        a[background(cc)] = TRANSPARENT
        sprites.append(a.reshape(-1))

    def table(name, arrays, comment):
        out = [f"/* {comment} */", f"const uint8_t {name}[{len(arrays)}][256] = {{"]
        for a in arrays:
            out.append("    {")
            for k in range(0, 256, 16):
                out.append("        " + ", ".join(f"0x{v:02X}" for v in a[k:k + 16]) + ",")
            out.append("    },")
        out.append("};")
        return "\n".join(out)

    c = ["/* made by tools/gen_gfx.py from art/tilesheet.png - edit the art or the tool, not this */",
         "#include <stdint.h>", '#include "gfx_data.h"', "",
         f"/* {len(pal3)} colours (9-bit RGB333); 0-15 are the original's pens */",
         "const uint16_t gfx_pal[256] = {"]
    for k in range(0, 256, 12):
        c.append("    " + ", ".join(f"0x{v:03X}" for v in pal[k:k + 12]) + ",")
    c += ["};", "", table("gfx_tiles", tiles, "Layer 2 tiles, column-major: " + " ".join(n for n, _, _ in TILES)), ""]
    asm = ["; made by tools/gen_gfx.py - sprite patterns: " + " ".join(n for n, _, _ in SPRITES),
           f"SECTION BANK_{SPR_BANK}"]
    for a in sprites:
        for k in range(0, 256, 16):
            asm.append("    defb " + ",".join(f"${v:02X}" for v in a[k:k + 16]))
    (HERE / "src" / "gfx_sprites.asm").write_text("\n".join(asm) + "\n")
    (HERE / "src" / "gfx_data.c").write_text("\n".join(c))
    h = ["/* made by tools/gen_gfx.py */", "#ifndef GFX_DATA_H", "#define GFX_DATA_H", "#include <stdint.h>",
         "extern const uint16_t gfx_pal[256];",
         f"extern const uint8_t gfx_tiles[{len(TILES)}][256];",
         f"#define SPR_PAGE {2 * SPR_BANK}         /* the sprite patterns' first 8K page */"]
    h += [f"#define TILE_{n} {i}" for i, (n, _, _) in enumerate(TILES)]
    h += [f"#define SPR_{n} {i}" for i, (n, _, _) in enumerate(SPRITES[:4])]
    h += ["#define SPR_ENEMY_A 4", "#define SPR_ENEMY_B 12", "#define SPR_ENEMY_C 20",
          f"#define NUM_SPRITE_PATTERNS {len(SPRITES)}", "#endif"]
    (HERE / "src" / "gfx_data.h").write_text("\n".join(h) + "\n")
    # a preview of what the Next shows
    prev = Image.new("RGB", (16 * 10, 16 * 4))
    rgb = [tuple(((v >> s) & 7) * 255 // 7 for s in (6, 3, 0)) for v in pal]
    for i, t in enumerate(tiles):
        img = Image.new("RGB", (16, 16))
        img.putdata([rgb[v] for v in t.reshape(16, 16).T.reshape(-1)])
        prev.paste(img, (16 * i, 0))
    for i, s in enumerate(sprites):
        img = Image.new("RGB", (16, 16))
        img.putdata([(40, 0, 40) if v == TRANSPARENT else rgb[v] for v in s])
        prev.paste(img, (16 * (i % 10), 16 + 16 * (i // 10)))
    prev.resize((prev.width * 4, prev.height * 4), Image.NEAREST).save(HERE / "build" / "gfx_preview.png")
    print(f"{len(pal3)} colours, {len(tiles)} tiles, {len(sprites)} sprite patterns")


if __name__ == "__main__":
    (HERE / "build").mkdir(exist_ok=True)
    main()
