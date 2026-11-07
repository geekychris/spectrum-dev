#!/usr/bin/env python3
"""Planet Chomp's pictures for the Next.

  gfx/stars_N.bin   the starfield (the original's render_cel.c: 90 stars on a
                    160 x 120 texture drawn at 2x, from its own random
                    numbers), 256 x 192 Layer 2 bytes in three 16K banks;
                    each frame starts as a DMA copy of it
  src/gfx_data.asm  those banks for the .nex

    python3 tools/gen_gfx.py
"""
from pathlib import Path

HERE = Path(__file__).resolve().parent.parent
C_SPACE, C_STAR1 = 0, 1
STARS_BANK = 22


def stars():
    """the original's sky texture: colour index per texel"""
    w, h = 160, 120
    tex = [[C_SPACE] * w for _ in range(h)]
    r = 7
    for _ in range(90):
        r = (r * 1103515245 + 12345) & 0xFFFFFFFF
        x = (r >> 16) % w
        r = (r * 1103515245 + 12345) & 0xFFFFFFFF
        y = (r >> 16) % h
        r = (r * 1103515245 + 12345) & 0xFFFFFFFF
        b = 110 + (r >> 16) % 146
        tex[y][x] = C_STAR1 + min(3, (b - 110) * 4 // 146)
    # 160 x 120 at 2x on the 320 x 240 frame = 256 x 192 here (x 1.6, nearest)
    out = bytearray(256 * 192)
    for y in range(192):
        for x in range(256):
            out[y * 256 + x] = tex[y * 120 // 192][x * 160 // 256]
    return out


def main():
    gfx = HERE / "gfx"
    gfx.mkdir(exist_ok=True)
    data = stars()
    asm = ["; made by tools/gen_gfx.py - do not edit: the starfield, three 16K banks",
           "; (l2_copy_to_back copies them into the back buffer each frame)", ""]
    for i in range(3):
        name = f"stars_{i}.bin"
        (gfx / name).write_bytes(data[i * 16384:(i + 1) * 16384])
        asm += [f"SECTION BANK_{STARS_BANK + i}", f'BINARY "gfx/{name}"']
    (HERE / "src" / "gfx_data.asm").write_text("\n".join(asm) + "\n")
    print(f"gen_gfx: starfield in banks {STARS_BANK}-{STARS_BANK + 2}")


if __name__ == "__main__":
    main()
