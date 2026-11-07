#!/usr/bin/env python3
"""The original's tune (tools/amiga_music.h, copied from its sound.c) as AY
patterns for the AY (main.c's music_tick plays them as the original did).

On the Amiga each channel plays a 32-sample waveform: a note's pitch is
3546895 / (period * 32) Hz. Every row lasts `speed` ticks; a 0 silences the
channel (so notes are a row long). The patterns are kept as note numbers;
the periods become the AY's. Writes src/music_data.c.
"""
import re
from pathlib import Path

HERE = Path(__file__).resolve().parent
SRC = (HERE / "amiga_music.h").read_text()
SRC = re.sub(r"/\*.*?\*/", "", SRC, flags=re.S)
AY_CLOCK = 1773400      # 128K AY


def array(name):
    body = re.search(name + r"\s*(\[[^=]*)?=\s*\{(.*?)\};", SRC, re.S).group(2)
    return [int(x) for x in re.findall(r"\d+", body)]


periods = array("note_periods")
melody = array("melody")
bass = array("bassline")
order = array("song_order")
speed = int(re.search(r"speed\s*=\s*(\d+)", SRC).group(1))
PAT = 64


out = ["/* made by tools/gen_music.py from the original's tune: do not edit */",
       "#include <stdint.h>", '#include "music_data.h"', ""]
ay = [0] + [round(AY_CLOCK / (16 * (3546895 / (p * 32)))) for p in periods[1:]]
out.append(f"/* AY tone periods of the original's notes 1..{len(ay) - 1} (0 = rest) */")
out.append(f"const uint16_t music_period[{len(ay)}] = {{")
for i in range(0, len(ay), 12):
    out.append("    " + ", ".join(str(v) for v in ay[i:i + 12]) + ",")
out.append("};")
for name, pats in (("music_melody", melody), ("music_bass", bass)):
    out.append(f"const uint8_t {name}[{len(pats)}] = {{")
    for i in range(0, len(pats), 16):
        out.append("    " + ", ".join(str(v) for v in pats[i:i + 16]) + ",")
    out.append("};")
out.append(f"const uint8_t music_order[{len(order)}] = {{ " + ", ".join(map(str, order)) + " };")
(HERE.parent / "src" / "music_data.c").write_text("\n".join(out) + "\n")
(HERE.parent / "src" / "music_data.h").write_text(
    "/* made by tools/gen_music.py: do not edit */\n#ifndef MUSIC_DATA_H\n#define MUSIC_DATA_H\n"
    "#include <stdint.h>\n"
    f"#define MUSIC_PATTERN {PAT}      /* rows a pattern */\n"
    f"#define MUSIC_SONG {len(order)}          /* patterns in the song */\n"
    f"#define MUSIC_SPEED {speed}         /* ticks a row */\n"
    f"extern const uint16_t music_period[{len(ay)}];\n"
    f"extern const uint8_t music_melody[{len(melody)}];   /* [pattern][row]: note, 0 = rest */\n"
    f"extern const uint8_t music_bass[{len(bass)}];\n"
    f"extern const uint8_t music_order[{len(order)}];\n#endif\n")
print(f"gen_music: {len(melody) + len(bass)} note bytes")
