"""ASCII art -> src/art.c: the 8 x 8 tiles (one per tile id, as cellspr48
draws them) and the 8 x 8 sprite frames ('#' = ink).

    python3 tools/make_art.py
"""
from pathlib import Path

HERE = Path(__file__).resolve().parents[1]

# tile ids 0..9 (game.h): empty brick solid ladder bar gold hidden player enemy trap
EMPTY = ["........"] * 8
BRICK = ["###.####", "###.####", "###.####", "........",
         "#######.", "#######.", "#######.", "........"]
SOLID = ["#######.", "#.#.#.#.", "##.#.##.", "#.#.#.#.",
         "##.#.##.", "#.#.#.#.", "#######.", "........"]
LADDER = [".#....#.", ".######.", ".#....#.", ".#....#.",
          ".#....#.", ".######.", ".#....#.", ".#....#."]
BAR = ["........", "########", "########", "#..#..#.",
       "........", "........", "........", "........"]
GOLD = ["........", "........", "..####..", ".#.##.#.",
        ".######.", "########", "########", "........"]
MARK_P = ["########", "#......#", "#.###..#", "#.#..#.#",
          "#.###..#", "#.#....#", "#......#", "########"]
MARK_E = ["########", "#......#", "#.####.#", "#.#....#",
          "#.###..#", "#.#....#", "#.####.#", "########"]
HIDDEN_ED = [".#....#.", "........", ".#....#.", "........",
             ".#....#.", "........", ".#....#.", "........"]
TRAP_ED = ["###.####", "###.####", "###.####", "........",
           "#######.", "##.#.##.", "#.#.#.#.", "........"]
TILES = [EMPTY, BRICK, SOLID, LADDER, BAR, GOLD, EMPTY, MARK_P, MARK_E, BRICK]
EDIT_TILES = [EMPTY, BRICK, SOLID, LADDER, BAR, GOLD, HIDDEN_ED, MARK_P, MARK_E, TRAP_ED]

# sprites, facing right (the left-facing copies are mirrored at start-up)
FRAMES = {
    "STAND": ["...##...", "...##...", ".######.", "#.####.#",
              "..####..", "..#..#..", "..#..#..", ".##..##."],
    "RUN1":  ["....##..", "....##..", "..####.#", ".#.###..",
              "...###..", "..##.#..", ".#....#.", "#.....#."],
    "RUN2":  ["....##..", "....##..", "..#####.", "..####..",
              "...###..", "...##...", "...#.#..", "..#..#.."],
    "CLIMB1": ["#..##...", "#..##...", "######..", "..####.#",
               "..####.#", "..#..#..", ".##..#..", ".....##."],
    "CLIMB2": ["...##..#", "...##..#", "..######", "#.####..",
               "#.####..", "..#..#..", "..#..##.", ".##....."],
    "HANG":  ["#......#", "#..##..#", "#.####.#", ".######.",
              "..####..", "..####..", "..#..#..", "..#..#.."],
    "DIG":   ["...##...", "...##...", "..####..", ".#####..",
              "..######", "..#..#.#", "..#..#..", ".##..##."],
    "FALL":  ["#..##..#", ".#.##.#.", "..####..", "..####..",
              "..####..", ".#....#.", "#......#", "........"],
    "TRAPPED": ["...##...", "...##...", ".######.", "#.####.#",
                "........", "........", "........", "........"],
    "CURSOR": ["##.##.##", "#......#", "........", "#......#",
               "#......#", "........", "#......#", "##.##.##"],
}


def bits(art):
    assert len(art) == 8 and all(len(r) == 8 for r in art), art
    return [int(r.replace("#", "1").replace(".", "0"), 2) for r in art]


def table(name, arts, comment):
    rows = [f"    {', '.join(f'0x{b:02X}' for b in bits(a))}," for a in arts]
    return f"/* {comment} */\nconst uint8_t {name}[] = {{\n" + "\n".join(rows) + "\n};\n"


out = ["/* made by tools/make_art.py - edit the art there */", "#include <stdint.h>", '#include "art.h"', ""]
out.append(table("tile_gfx", TILES, "tile ids 0..9 in play"))
out.append(table("edit_gfx", EDIT_TILES, "in the editor: hidden ladders and traps show"))
out.append(table("frame_gfx", list(FRAMES.values()), "sprite frames (facing right): " + " ".join(FRAMES)))
(HERE / "src" / "art.c").write_text("\n".join(out))
hdr = ["/* made by tools/make_art.py */", "#ifndef ART_H", "#define ART_H", "#include <stdint.h>",
       "extern const uint8_t tile_gfx[], edit_gfx[], frame_gfx[];"]
hdr += [f"#define FR_{n} {i}" for i, n in enumerate(FRAMES)]
hdr += [f"#define NUM_FRAMES {len(FRAMES)}", "#endif"]
(HERE / "src" / "art.h").write_text("\n".join(hdr) + "\n")
print("src/art.c", len(FRAMES), "frames")
